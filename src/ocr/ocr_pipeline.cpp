#include "ocr/ocr_pipeline.h"

#include <array>
#include <chrono>
#include <cstddef>

namespace tmw::ocr {
namespace {

double millisecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

}  // namespace

OcrPipeline::OcrPipeline(const std::filesystem::path& detectionModelDir,
                         const std::filesystem::path& recognitionModelDir, Device device,
                         const OcrOptions& options)
    : options_(options),
      detector_(detectionModelDir, device, options.detection),
      recognizer_(recognitionModelDir, device) {
    if (options_.warmUpOnStart) {
        warmUp();
    }
}

void OcrPipeline::warmUp() {
    // 偵測：用固定輸入的大小（沒設定時用透鏡常見的大小），辨識：最常見的寬度級距
    const cv::Size detectionSize = options_.detection.fixedInput.area() > 0
                                       ? options_.detection.fixedInput
                                       : cv::Size(960, 544);
    const cv::Mat frame(detectionSize, CV_8UC3, cv::Scalar(255, 255, 255));
    detector_.detect(frame);

    const cv::Mat crop(48, 512, CV_8UC3, cv::Scalar(255, 255, 255));
    if (options_.batchRecognition) {
        const std::array<cv::Mat, 1> crops{crop};
        recognizer_.recognize(crops, options_.maxBatch);
    } else {
        recognizer_.recognize(crop);
    }
}

std::vector<TextLine> OcrPipeline::run(const cv::Mat& bgr, OcrTimings* timings) {
    const auto detectionStart = std::chrono::steady_clock::now();
    std::vector<DetectedBox> boxes = detector_.detect(bgr);
    sortBoxes(boxes);
    const double detectionMs = millisecondsSince(detectionStart);

    const auto recognitionStart = std::chrono::steady_clock::now();
    std::vector<cv::Mat> crops;
    crops.reserve(boxes.size());
    for (const DetectedBox& box : boxes) {
        crops.push_back(cropTextRegion(bgr, box.points));
    }

    std::vector<Recognition> recognitions;
    if (options_.batchRecognition) {
        recognitions = recognizer_.recognize(crops, options_.maxBatch);
    } else {
        recognitions.resize(crops.size());
        for (std::size_t i = 0; i < crops.size(); ++i) {
            if (!crops[i].empty()) {
                recognitions[i] = recognizer_.recognize(crops[i]);
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
    if (timings != nullptr) {
        timings->detectionMs = detectionMs;
        timings->recognitionMs = millisecondsSince(recognitionStart);
        timings->boxes = static_cast<int>(boxes.size());
    }
    return lines;
}

}  // namespace tmw::ocr
