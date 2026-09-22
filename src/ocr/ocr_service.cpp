#include "ocr/ocr_service.h"

#include <algorithm>
#include <cstdint>
#include <opencv2/imgproc.hpp>
#include <utility>

namespace tmw::ocr {

cv::Mat toBgr(const core::ImageBgra& frame) {
    if (frame.empty()) {
        return {};
    }
    const cv::Mat bgra(frame.height, frame.width, CV_8UC4,
                       const_cast<std::uint8_t*>(frame.pixels.data()));
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
}

core::OcrLine toOcrLine(const TextLine& line) {
    int left = line.box[0].x;
    int top = line.box[0].y;
    int right = left;
    int bottom = top;
    for (const cv::Point& point : line.box) {
        left = std::min(left, point.x);
        top = std::min(top, point.y);
        right = std::max(right, point.x);
        bottom = std::max(bottom, point.y);
    }
    core::OcrLine out;
    out.rect = core::RectI{left, top, right, bottom};
    out.text = line.text;
    out.score = line.score;
    // design.md 4.4：高度大於寬度的 1.5 倍就當成直排
    out.orientation = out.rect.height() > out.rect.width() * 3 / 2 ? core::Orientation::Vertical
                                                                   : core::Orientation::Horizontal;
    return out;
}

OcrService::OcrService(const std::filesystem::path& modelsDirectory, TextLanguage language,
                       Device device, const OcrOptions& options)
    : pipeline_(detectionModelPath(modelsDirectory, chooseModels(language, device)),
                recognitionModelPath(modelsDirectory, chooseModels(language, device)), device,
                options) {}

std::vector<core::OcrLine> OcrService::recognize(const core::ImageBgra& frame,
                                                 std::stop_token cancel) {
    if (frame.empty() || cancel.stop_requested()) {
        return {};
    }
    const cv::Mat bgr = toBgr(frame);
    const std::vector<TextLine> lines = pipeline_.run(bgr, &lastTimings_);
    if (cancel.stop_requested()) {
        return {};
    }
    std::vector<core::OcrLine> out;
    out.reserve(lines.size());
    for (const TextLine& line : lines) {
        out.push_back(toOcrLine(line));
    }
    return out;
}

}  // namespace tmw::ocr
