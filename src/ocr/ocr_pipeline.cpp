#include "ocr/ocr_pipeline.h"

#include <chrono>

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
      recognizer_(recognitionModelDir, device) {}

std::vector<TextLine> OcrPipeline::run(const cv::Mat& bgr, OcrTimings* timings) {
    const auto detectionStart = std::chrono::steady_clock::now();
    std::vector<DetectedBox> boxes = detector_.detect(bgr);
    sortBoxes(boxes);
    const double detectionMs = millisecondsSince(detectionStart);

    const auto recognitionStart = std::chrono::steady_clock::now();
    std::vector<TextLine> lines;
    lines.reserve(boxes.size());
    for (const DetectedBox& box : boxes) {
        const cv::Mat crop = cropTextRegion(bgr, box.points);
        if (crop.empty()) {
            continue;
        }
        Recognition recognition = recognizer_.recognize(crop);
        if (recognition.score < options_.recognitionScoreThreshold) {
            continue;
        }
        lines.push_back({box.points, std::move(recognition.text), recognition.score, box.score});
    }
    if (timings != nullptr) {
        timings->detectionMs = detectionMs;
        timings->recognitionMs = millisecondsSince(recognitionStart);
        timings->boxes = static_cast<int>(boxes.size());
    }
    return lines;
}

}  // namespace tmw::ocr
