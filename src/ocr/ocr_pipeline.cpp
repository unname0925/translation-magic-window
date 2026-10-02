#include "ocr/ocr_pipeline.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace tmw::ocr {
namespace {

double millisecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

// 主模型讀出來的結果，看起來像不像「在韓文上讀空了」（core::worthTryingKorean）
bool mayBeKorean(const std::vector<TextLine>& lines) {
    int hollow = 0;
    for (const TextLine& line : lines) {
        const auto side = [&](int a, int b) {
            return std::hypot(static_cast<double>(line.box[a].x - line.box[b].x),
                              static_cast<double>(line.box[a].y - line.box[b].y));
        };
        const double width = std::max(side(0, 1), side(2, 3));
        const double height = std::max(side(1, 2), side(3, 0));
        const double expected = std::max(width, height) / std::max(1.0, std::min(width, height));
        if (core::readsAsHollow(line.text, expected)) {
            ++hollow;
        }
    }
    return core::worthTryingKorean(hollow, static_cast<int>(lines.size()));
}

}  // namespace

OcrPipeline::OcrPipeline(const std::filesystem::path& detectionModelDir,
                         const std::filesystem::path& recognitionModelDir, Device device,
                         const OcrOptions& options)
    : OcrPipeline(detectionModelDir, recognitionModelDir, {}, device, options) {}

OcrPipeline::OcrPipeline(const std::filesystem::path& detectionModelDir,
                         const std::filesystem::path& recognitionModelDir,
                         const std::filesystem::path& koreanRecognitionModelDir, Device device,
                         const OcrOptions& options)
    : options_(options),
      detector_(detectionModelDir, device, options.detection),
      mainDirectory_(recognitionModelDir),
      device_(device) {
    if (!koreanRecognitionModelDir.empty()) {
        korean_.emplace(koreanRecognitionModelDir, device);
    }
    // 指定韓文時主模型用不到，先不載入（省下約 117 MB，M2-04）；之後改了語言才在第一次用到時載入。
    // 資料夾不在就現在報錯，不要等到執行到一半
    if (options_.warmUpScript != core::Language::Korean || !korean_.has_value()) {
        mainRecognizer();
    } else if (!std::filesystem::exists(mainDirectory_)) {
        throw std::runtime_error("找不到辨識模型：" + mainDirectory_.string());
    }
    if (options_.warmUpOnStart) {
        warmUp();
    }
}

TextRecognizer& OcrPipeline::mainRecognizer() {
    if (!main_.has_value()) {
        main_.emplace(mainDirectory_, device_);
    }
    return *main_;
}

cv::Size lensDetectionInput() {
    return {960, 544};
}

void OcrPipeline::warmUp() {
    // 偵測：用固定輸入的大小（沒設定時用透鏡常見的大小），辨識：最常見的寬度級距
    const cv::Size detectionSize = options_.detection.fixedInput.area() > 0
                                       ? options_.detection.fixedInput
                                       : lensDetectionInput();
    const cv::Mat frame(detectionSize, CV_8UC3, cv::Scalar(255, 255, 255));
    detector_.detect(frame);

    TextRecognizer& recognizer =
        options_.warmUpScript == core::Language::Korean && korean_.has_value() ? *korean_
                                                                               : mainRecognizer();
    const cv::Mat crop(48, 512, CV_8UC3, cv::Scalar(255, 255, 255));
    if (options_.batchRecognition) {
        const std::array<cv::Mat, 1> crops{crop};
        recognizer.recognize(crops, options_.maxBatch);
    } else {
        recognizer.recognize(crop);
    }
}

std::vector<TextLine> OcrPipeline::recognizeCrops(TextRecognizer& recognizer,
                                                  const std::vector<DetectedBox>& boxes,
                                                  const std::vector<cv::Mat>& crops) {
    std::vector<Recognition> recognitions;
    if (options_.batchRecognition) {
        recognitions = recognizer.recognize(crops, options_.maxBatch);
    } else {
        recognitions.resize(crops.size());
        for (std::size_t i = 0; i < crops.size(); ++i) {
            if (!crops[i].empty()) {
                recognitions[i] = recognizer.recognize(crops[i]);
            }
        }
    }

    std::vector<TextLine> lines;
    lines.reserve(boxes.size());
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        if (crops[i].empty() || recognitions[i].score < options_.recognitionScoreThreshold) {
            continue;
        }
        lines.push_back({boxes[i].points, std::move(recognitions[i].text), recognitions[i].score,
                         boxes[i].score});
    }
    return lines;
}

std::vector<TextLine> OcrPipeline::run(const cv::Mat& bgr, OcrTimings* timings) {
    return run(bgr, core::Language::Unknown, timings).lines;
}

OcrRun OcrPipeline::run(const cv::Mat& bgr, core::Language script, OcrTimings* timings) {
    const auto detectionStart = std::chrono::steady_clock::now();
    std::vector<DetectedBox> boxes = detector_.detect(bgr);
    sortBoxes(boxes);
    const double detectionMs = millisecondsSince(detectionStart);

    const auto recognitionStart = std::chrono::steady_clock::now();
    // 偵測只做一次，兩個辨識模型讀的是同一批裁切圖
    std::vector<cv::Mat> crops;
    crops.reserve(boxes.size());
    for (const DetectedBox& box : boxes) {
        crops.push_back(cropTextRegion(bgr, box.points));
    }

    OcrRun out;
    if (!korean_.has_value()) {
        out.lines = recognizeCrops(mainRecognizer(), boxes, crops);
        out.script = core::Language::Unknown;  // 沒有第二個模型，沒有什麼好判斷的
    } else if (script == core::Language::Korean) {
        out.lines = recognizeCrops(*korean_, boxes, crops);
        out.script = core::Language::Korean;
    } else if (script != core::Language::Unknown) {
        out.lines = recognizeCrops(mainRecognizer(), boxes, crops);
        out.script = script;
    } else {
        // 還不知道是哪種文字：先讀主模型，看起來可能是韓文才叫韓文模型，
        // 再整張一起決定（design.md 4.4「語言判斷」）。
        // 逐行比分數不可行：PP-OCRv6 讀韓文會給亂讀的結果很高的分數。
        std::vector<TextLine> main = recognizeCrops(mainRecognizer(), boxes, crops);
        const auto join = [](const std::vector<TextLine>& lines) {
            std::string text;
            for (const TextLine& line : lines) {
                text += line.text;
            }
            return text;
        };
        if (!mayBeKorean(main)) {
            out.script = core::chooseScript(join(main), {});
            out.lines = std::move(main);
        } else {
            std::vector<TextLine> korean = recognizeCrops(*korean_, boxes, crops);
            out.script = core::chooseScript(join(main), join(korean));
            out.lines = out.script == core::Language::Korean ? std::move(korean) : std::move(main);
        }
    }

    if (timings != nullptr) {
        timings->detectionMs = detectionMs;
        timings->recognitionMs = millisecondsSince(recognitionStart);
        timings->boxes = static_cast<int>(boxes.size());
    }
    return out;
}

}  // namespace tmw::ocr
