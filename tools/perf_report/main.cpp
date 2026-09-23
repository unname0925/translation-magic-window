// tmw_perf_report：量測每個步驟的耗時，並對照 docs/design.md 第 5 節的預算（M1-15）。
//
//   tmw_perf_report --models <模型資料夾> [--device dml|cpu|auto] [--repeat N]
//                   [--language ja|en|ko] [--fixed-input W H] [--crop W H]
//                   [--json <報告.json>] <圖片> [<圖片> ...]
//
// --fixed-input W H：偵測固定用這個輸入大小（主程式用 ocr::lensDetectionInput()）。
// --crop W H：從圖片中間裁一塊，模擬透鏡實際擷取到的範圍。主程式從來不會 OCR 整張畫面，
//             拿整頁來量會得到和產品無關的數字。
//
// 每張圖片先跑一次暖機（不計入），再跑 N 次。量的是 OCR 和分段——這兩步在本機、每次都跑，
// 也是我們控制得了的部分。翻譯是網路或 LLM 決定的，數字看 M0-12 的實測和程式的除錯傾印。
//
// 報告看的是中位數和 p95：平均值會被少數幾次很慢的處理拉走，而使用者抱怨的正是偶爾卡的那幾次。
#include <windows.h>

#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include "core/perf_stats.h"
#include "core/ruby.h"
#include "core/text_layout.h"
#include "ocr/model_choice.h"
#include "ocr/ocr_pipeline.h"
#include "ocr/ocr_service.h"
#include "ocr/onnx_model.h"
#include "platform/png_file.h"
#include "platform/text_encoding.h"

namespace {

struct Arguments {
    std::filesystem::path models;
    tmw::ocr::Device device = tmw::ocr::Device::Auto;
    tmw::ocr::TextLanguage language = tmw::ocr::TextLanguage::JapaneseOrEnglish;
    int repeat = 10;
    int fixedWidth = 0;  // --fixed-input W H：偵測固定用這個輸入大小
    int fixedHeight = 0;
    int cropWidth = 0;  // --crop W H：從中間裁一塊，模擬透鏡實際擷取到的範圍
    int cropHeight = 0;
    std::filesystem::path json;
    std::vector<std::filesystem::path> images;
};

void printUsage() {
    std::fputs(
        "usage: tmw_perf_report --models <dir> [--device dml|cpu|auto] [--repeat N]\n"
        "                       [--language ja|en|ko] [--fixed-input W H] [--crop W H]\n"
        "                       [--json <report.json>] <image> [<image> ...]\n",
        stderr);
}

std::optional<Arguments> parseArguments(int argc, wchar_t** argv) {
    Arguments args;
    for (int i = 1; i < argc; ++i) {
        const std::wstring option = argv[i];
        const auto next = [&](const wchar_t* name) -> std::optional<std::wstring> {
            if (i + 1 >= argc) {
                std::fwprintf(stderr, L"%ls 後面少了值\n", name);
                return std::nullopt;
            }
            return argv[++i];
        };
        if (option == L"--models") {
            const auto value = next(L"--models");
            if (!value) {
                return std::nullopt;
            }
            args.models = *value;
        } else if (option == L"--device") {
            const auto value = next(L"--device");
            if (!value) {
                return std::nullopt;
            }
            args.device = *value == L"cpu"   ? tmw::ocr::Device::Cpu
                          : *value == L"dml" ? tmw::ocr::Device::DirectML
                                             : tmw::ocr::Device::Auto;
        } else if (option == L"--language") {
            const auto value = next(L"--language");
            if (!value) {
                return std::nullopt;
            }
            args.language = *value == L"ko" ? tmw::ocr::TextLanguage::Korean
                                            : tmw::ocr::TextLanguage::JapaneseOrEnglish;
        } else if (option == L"--fixed-input") {
            if (i + 2 >= argc) {
                std::fputs("--fixed-input 後面要接寬和高\n", stderr);
                return std::nullopt;
            }
            args.fixedWidth = std::stoi(argv[++i]);
            args.fixedHeight = std::stoi(argv[++i]);
        } else if (option == L"--crop") {
            if (i + 2 >= argc) {
                std::fputs("--crop 後面要接寬和高\n", stderr);
                return std::nullopt;
            }
            args.cropWidth = std::stoi(argv[++i]);
            args.cropHeight = std::stoi(argv[++i]);
        } else if (option == L"--repeat") {
            const auto value = next(L"--repeat");
            if (!value) {
                return std::nullopt;
            }
            args.repeat = std::stoi(*value);
        } else if (option == L"--json") {
            const auto value = next(L"--json");
            if (!value) {
                return std::nullopt;
            }
            args.json = *value;
        } else if (option.starts_with(L"--")) {
            std::fwprintf(stderr, L"不認得的選項：%ls\n", option.c_str());
            return std::nullopt;
        } else {
            args.images.push_back(option);
        }
    }
    if (args.models.empty() || args.images.empty() || args.repeat < 1) {
        printUsage();
        return std::nullopt;
    }
    return args;
}

// 從畫面中間裁一塊，模擬透鏡實際會擷取到的範圍。寬或高是 0 就原樣回傳。
tmw::core::ImageBgra centreCrop(const tmw::core::ImageBgra& frame, int width, int height) {
    if (width <= 0 || height <= 0 || frame.empty()) {
        return frame;
    }
    width = std::min(width, frame.width);
    height = std::min(height, frame.height);
    const int left = (frame.width - width) / 2;
    const int top = (frame.height - height) / 2;

    tmw::core::ImageBgra out(width, height);
    for (int y = 0; y < height; ++y) {
        const std::size_t source = (static_cast<std::size_t>(top + y) * frame.width + left) * 4;
        const std::size_t target = static_cast<std::size_t>(y) * width * 4;
        std::copy_n(frame.pixels.begin() + source, static_cast<std::size_t>(width) * 4,
                    out.pixels.begin() + target);
    }
    return out;
}

double millisecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const std::optional<Arguments> args = parseArguments(argc, argv);
    if (!args) {
        return 2;
    }
    SetConsoleOutputCP(CP_UTF8);  // 不設的話中文會讓輸出從那裡斷掉
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // PNG 走 WIC

    try {
        tmw::ocr::OcrOptions options;
        options.detection.fixedInput = {args->fixedWidth, args->fixedHeight};
        tmw::ocr::OcrService ocr(args->models, args->language, args->device, options);
        std::printf("裝置：%s\n", std::string(tmw::ocr::deviceName(ocr.device())).c_str());

        tmw::core::PerfStats overall;
        nlohmann::json report;
        report["device"] = std::string(tmw::ocr::deviceName(ocr.device()));
        report["repeat"] = args->repeat;
        report["images"] = nlohmann::json::array();

        for (const std::filesystem::path& image : args->images) {
            const tmw::core::ImageBgra frame =
                centreCrop(tmw::platform::loadImage(image), args->cropWidth, args->cropHeight);
            tmw::core::PerfStats perImage;
            std::vector<double> detection;
            std::vector<double> recognition;
            int boxes = 0;

            // 第一次是暖機：模型第一次看到這個輸入大小時特別慢（design.md 4.4）
            for (int run = 0; run <= args->repeat; ++run) {
                tmw::core::PipelineTimings timings;

                const auto ocrStart = std::chrono::steady_clock::now();
                std::vector<tmw::core::OcrLine> lines = ocr.recognize(frame, std::stop_token{});
                timings.ocrMs = millisecondsSince(ocrStart);
                const tmw::ocr::OcrTimings inside = ocr.lastTimings();

                const auto layoutStart = std::chrono::steady_clock::now();
                const tmw::core::RubyResult withRuby = tmw::core::attachRuby(lines, {});
                const std::vector<tmw::core::TextBlock> blocks =
                    tmw::core::mergeIntoBlocks(withRuby.lines, {});
                timings.layoutMs = millisecondsSince(layoutStart);

                if (run == 0) {
                    std::printf("%s：%d×%d，%zu 行 → %zu 段\n",
                                tmw::platform::wideToUtf8(image.filename().wstring()).c_str(),
                                frame.width, frame.height, lines.size(), blocks.size());
                    continue;
                }
                perImage.add(timings);
                overall.add(timings);
                detection.push_back(inside.detectionMs);
                recognition.push_back(inside.recognitionMs);
                boxes = inside.boxes;
            }

            const tmw::core::PerfSummary detectionSummary = tmw::core::summarize(detection);
            const tmw::core::PerfSummary recognitionSummary = tmw::core::summarize(recognition);
            std::printf("\n%s（%d×%d，%d 個文字框）\n%s",
                        tmw::platform::wideToUtf8(image.filename().wstring()).c_str(), frame.width,
                        frame.height, boxes, perImage.report().c_str());
            // OCR 慢的時候要看得出是偵測還是辨識：偵測的輸入大小是固定的，辨識隨文字框數量長
            std::printf("　其中：偵測 %.1f ms、辨識 %.1f ms（中位數）\n", detectionSummary.median,
                        recognitionSummary.median);

            report["images"].push_back({{"file", tmw::platform::wideToUtf8(image.wstring())},
                                        {"width", frame.width},
                                        {"height", frame.height},
                                        {"boxes", boxes},
                                        {"ocrMedianMs", perImage.ocr().median},
                                        {"ocrP95Ms", perImage.ocr().p95},
                                        {"detectionMedianMs", detectionSummary.median},
                                        {"recognitionMedianMs", recognitionSummary.median},
                                        {"layoutMedianMs", perImage.layout().median},
                                        {"layoutP95Ms", perImage.layout().p95}});
        }

        const std::string summary = overall.report();
        std::printf("\n全部加起來\n%s", summary.c_str());
        report["summary"] = summary;
        report["ocrMedianMs"] = overall.ocr().median;
        report["ocrP95Ms"] = overall.ocr().p95;

        if (!args->json.empty()) {
            std::ofstream out(args->json, std::ios::binary);
            out << report.dump(2) << "\n";
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "失敗：%s\n", error.what());
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        return 1;
    }

    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return 0;
}
