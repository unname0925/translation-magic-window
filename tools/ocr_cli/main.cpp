// tmw_ocr_cli：對圖片跑 OCR，把結果和各步驟的耗時寫成 JSON。
//
//   tmw_ocr_cli --det <偵測模型資料夾> --rec <辨識模型資料夾> [--device cpu|dml]
//               [--repeat N] --output <結果.json> <圖片> [<圖片> ...]
//   tmw_ocr_cli --rec <辨識模型資料夾> --dump-characters <字元表.json>
//
// --repeat N：每張圖片跑 N 次。第一次是暖機，耗時取其餘幾次的中位數；
// 每次的結果也必須完全相同，否則標記為 deterministic=false。
// JSON 格式和 tools/eval/ocr_reference.py 相同，用 tools/eval/compare_ocr.py 比對。
// --dump-characters：把 C++ 讀到的 CTC 字元表寫成 JSON 陣列，用來和 Python（PyYAML）逐字比對。

#include <windows.h>

#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <opencv2/core/utils/logger.hpp>
#include <opencv2/imgproc.hpp>
#include <optional>
#include <string>
#include <vector>

#include "ocr/ocr_pipeline.h"
#include "platform/png_file.h"
#include "platform/text_encoding.h"

namespace {

using tmw::ocr::Device;
using tmw::ocr::OcrOptions;
using tmw::ocr::OcrPipeline;
using tmw::ocr::OcrTimings;
using tmw::ocr::TextLine;

struct Arguments {
    std::filesystem::path detectionModel;
    std::filesystem::path recognitionModel;
    Device device = Device::Cpu;
    int repeat = 1;
    bool batchRecognition = true;  // --no-batch：逐行辨識（和官方版一致，用來比對差異）
    int maxBatch = 64;
    int fixedWidth = 0;  // --fixed-input W H：偵測固定用這個輸入大小
    int fixedHeight = 0;
    std::filesystem::path output;
    std::filesystem::path dumpCharacters;
    std::vector<std::filesystem::path> images;
};

void printUsage() {
    std::fputs(
        "usage: tmw_ocr_cli --det <dir> --rec <dir> [--device cpu|dml|auto] [--repeat N]\n"
        "                   [--no-batch] [--max-batch N] [--fixed-input W H]\n"
        "                   --output <result.json> <image> [<image> ...]\n"
        "       tmw_ocr_cli --rec <dir> --dump-characters <characters.json>\n",
        stderr);
}

std::optional<Arguments> parseArguments(int argc, wchar_t** argv) {
    Arguments args;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == L"--det" && hasValue) {
            args.detectionModel = argv[++i];
        } else if (arg == L"--rec" && hasValue) {
            args.recognitionModel = argv[++i];
        } else if (arg == L"--device" && hasValue) {
            const std::wstring value = argv[++i];
            if (value == L"cpu") {
                args.device = Device::Cpu;
            } else if (value == L"dml") {
                args.device = Device::DirectML;
            } else if (value == L"auto") {
                args.device = Device::Auto;
            } else {
                return std::nullopt;
            }
        } else if (arg == L"--repeat" && hasValue) {
            args.repeat = std::max(1, _wtoi(argv[++i]));
        } else if (arg == L"--no-batch") {
            args.batchRecognition = false;
        } else if (arg == L"--max-batch" && hasValue) {
            args.maxBatch = std::max(1, _wtoi(argv[++i]));
        } else if (arg == L"--fixed-input" && i + 2 < argc) {
            args.fixedWidth = std::max(1, _wtoi(argv[++i]));
            args.fixedHeight = std::max(1, _wtoi(argv[++i]));
        } else if (arg == L"--output" && hasValue) {
            args.output = argv[++i];
        } else if (arg == L"--dump-characters" && hasValue) {
            args.dumpCharacters = argv[++i];
        } else if (!arg.empty() && arg[0] == L'-') {
            return std::nullopt;
        } else {
            args.images.emplace_back(arg);
        }
    }
    if (!args.dumpCharacters.empty()) {
        return args.recognitionModel.empty() ? std::nullopt : std::optional(args);
    }
    if (args.detectionModel.empty() || args.recognitionModel.empty() || args.output.empty() ||
        args.images.empty()) {
        return std::nullopt;
    }
    return args;
}

std::string utf8(const std::filesystem::path& path) {
    return tmw::platform::wideToUtf8(path.wstring());
}

cv::Mat loadBgr(const std::filesystem::path& path) {
    tmw::core::ImageBgra image = tmw::platform::loadImage(path);
    const cv::Mat bgra(image.height, image.width, CV_8UC4, image.pixels.data(), image.stride());
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
}

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const size_t mid = values.size() / 2;
    return values.size() % 2 == 1 ? values[mid] : (values[mid - 1] + values[mid]) / 2.0;
}

bool sameLines(const std::vector<TextLine>& a, const std::vector<TextLine>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].box != b[i].box || a[i].text != b[i].text) {
            return false;
        }
    }
    return true;
}

nlohmann::json toJson(const std::vector<TextLine>& lines) {
    nlohmann::json result = nlohmann::json::array();
    for (const TextLine& line : lines) {
        nlohmann::json box = nlohmann::json::array();
        for (const cv::Point& p : line.box) {
            box.push_back({p.x, p.y});
        }
        result.push_back({{"box", box},
                          {"text", line.text},
                          {"score", line.score},
                          {"box_score", line.boxScore}});
    }
    return result;
}

int dumpCharacters(const Arguments& args) {
    const tmw::ocr::RecognitionModelConfig config =
        tmw::ocr::loadRecognitionModelConfig(args.recognitionModel / "inference.yml");
    std::ofstream out(args.dumpCharacters, std::ios::binary);
    out << nlohmann::json(config.characters).dump() << "\n";
    std::printf("%zu characters\n", config.characters.size());
    return out ? 0 : 1;
}

int run(const Arguments& args) {
    if (!args.dumpCharacters.empty()) {
        return dumpCharacters(args);
    }
    const auto loadStart = std::chrono::steady_clock::now();
    OcrOptions options;
    options.batchRecognition = args.batchRecognition;
    options.maxBatch = args.maxBatch;
    options.detection.fixedInput = cv::Size(args.fixedWidth, args.fixedHeight);
    OcrPipeline pipeline(args.detectionModel, args.recognitionModel, args.device, options);
    const double loadMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - loadStart)
            .count();
    // --device auto 時，印出實際選到的裝置
    std::printf("device=%s models loaded in %.0f ms\n",
                tmw::ocr::deviceName(pipeline.device()).data(), loadMs);

    nlohmann::json images = nlohmann::json::array();
    for (const std::filesystem::path& path : args.images) {
        const cv::Mat bgr = loadBgr(path);
        std::vector<TextLine> first;
        std::vector<double> detectionMs;
        std::vector<double> recognitionMs;
        bool deterministic = true;
        int boxes = 0;
        for (int i = 0; i < args.repeat; ++i) {
            OcrTimings timings;
            std::vector<TextLine> lines = pipeline.run(bgr, &timings);
            if (i == 0) {
                first = std::move(lines);
                boxes = timings.boxes;
            } else {
                deterministic = deterministic && sameLines(first, lines);
            }
            // 第一次是暖機（DirectML 第一次遇到新的輸入大小時要編譯），只有跑一次時才計入
            if (i > 0 || args.repeat == 1) {
                detectionMs.push_back(timings.detectionMs);
                recognitionMs.push_back(timings.recognitionMs);
            }
        }
        const double detection = median(detectionMs);
        const double recognition = median(recognitionMs);
        std::printf("%s: %zu lines, %d boxes, det %.1f ms, rec %.1f ms%s\n",
                    utf8(path.filename()).c_str(), first.size(), boxes, detection, recognition,
                    deterministic ? "" : "  [NOT DETERMINISTIC]");
        images.push_back(
            {{"image", utf8(path.filename())},
             {"width", bgr.cols},
             {"height", bgr.rows},
             {"lines", toJson(first)},
             {"deterministic", deterministic},
             {"timings_ms", {{"detection", detection}, {"recognition", recognition}}}});
    }

    const nlohmann::json result = {{"implementation", "cpp"},
                                   {"device", std::string(tmw::ocr::deviceName(args.device))},
                                   {"detection_model", utf8(args.detectionModel.filename())},
                                   {"recognition_model", utf8(args.recognitionModel.filename())},
                                   {"repeat", args.repeat},
                                   {"model_load_ms", loadMs},
                                   {"images", images}};
    std::ofstream out(args.output, std::ios::binary);
    out << result.dump(2) << "\n";
    return out ? 0 : 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const std::optional<Arguments> args = parseArguments(argc, argv);
    if (!args) {
        printUsage();
        return 2;
    }
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        std::fputs("error: CoInitializeEx failed\n", stderr);
        return 1;
    }
    int exitCode = 1;
    try {
        exitCode = run(*args);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
    }
    CoUninitialize();
    return exitCode;
}
