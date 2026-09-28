#include "ocr/comic_text_detector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace tmw::ocr {
namespace {

struct Candidate {
    std::array<double, 4> box;  // 原圖座標的左、上、右、下（還沒裁切、還沒取整數）
    float score = 0.0f;
    int label = 0;
};

double intersectionOverUnion(const std::array<double, 4>& a, const std::array<double, 4>& b) {
    const double width = std::max(0.0, std::min(a[2], b[2]) - std::max(a[0], b[0]));
    const double height = std::max(0.0, std::min(a[3], b[3]) - std::max(a[1], b[1]));
    const double shared = width * height;
    const double areaA = (a[2] - a[0]) * (a[3] - a[1]);
    const double areaB = (b[2] - b[0]) * (b[3] - b[1]);
    return shared / std::max(areaA + areaB - shared, 1e-6);
}

// Python 的 round() 是「四捨六入五成雙」；nearbyint 在預設的捨入模式下也是
int roundHalfEven(double value) {
    return static_cast<int>(std::nearbyint(value));
}

}  // namespace

std::vector<ComicTextBlock> decodeComicTextBlocks(std::span<const float> candidates, double scale,
                                                  core::SizeI imageSize,
                                                  const ComicTextOptions& options) {
    if (candidates.size() % kComicTextCandidateStride != 0) {
        throw std::invalid_argument("comic-text-detector 的候選框資料長度不是 7 的倍數");
    }
    if (scale <= 0.0) {
        throw std::invalid_argument("comic-text-detector 的縮放倍數必須大於 0");
    }

    std::vector<Candidate> kept;
    for (std::size_t offset = 0; offset < candidates.size(); offset += kComicTextCandidateStride) {
        const float* c = candidates.data() + offset;
        // 分數＝物件分數×類別分數，取兩個類別裡高的那個
        const float first = c[5] * c[4];
        const float second = c[6] * c[4];
        const int label = second > first ? 1 : 0;
        const float score = std::max(first, second);
        if (!(score > options.confidence)) {
            continue;
        }
        const double halfWidth = c[2] / 2.0;
        const double halfHeight = c[3] / 2.0;
        kept.push_back({{(c[0] - halfWidth) / scale, (c[1] - halfHeight) / scale,
                         (c[0] + halfWidth) / scale, (c[1] + halfHeight) / scale},
                        score,
                        label});
    }

    // NMS：分數由高到低，和已經留下的框重疊太多的丟掉
    std::stable_sort(kept.begin(), kept.end(),
                     [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    std::vector<Candidate> chosen;
    for (const Candidate& candidate : kept) {
        const bool overlapsChosen =
            std::any_of(chosen.begin(), chosen.end(), [&](const Candidate& other) {
                return intersectionOverUnion(candidate.box, other.box) > options.nmsIou;
            });
        if (!overlapsChosen) {
            chosen.push_back(candidate);
        }
    }

    std::vector<ComicTextBlock> blocks;
    for (const Candidate& candidate : chosen) {
        // 左上取整數時直接捨去小數、右下四捨五入，和參考實作一致
        const core::RectI rect{std::max(0, static_cast<int>(candidate.box[0])),
                               std::max(0, static_cast<int>(candidate.box[1])),
                               std::min(imageSize.width, roundHalfEven(candidate.box[2])),
                               std::min(imageSize.height, roundHalfEven(candidate.box[3]))};
        if (rect.right > rect.left && rect.bottom > rect.top) {
            blocks.push_back({rect, candidate.score, candidate.label});
        }
    }
    return blocks;
}

ComicTextDetector::ComicTextDetector(const std::filesystem::path& modelFile, Device device,
                                     const ComicTextOptions& options)
    : options_(options), model_(modelFile, device, "blk") {}

std::vector<ComicTextBlock> ComicTextDetector::detect(const cv::Mat& bgr) {
    if (bgr.empty()) {
        return {};
    }
    const double scale = static_cast<double>(kInputSize) / std::max(bgr.cols, bgr.rows);
    const cv::Size scaled{std::max(1, roundHalfEven(bgr.cols * scale)),
                          std::max(1, roundHalfEven(bgr.rows * scale))};

    // 縮小時用 INTER_AREA：參考實作用的 PIL 雙線性在縮小時會先做平滑，OpenCV 的
    // INTER_LINEAR 不會，兩者在細字上差很多。放大時兩者一致。
    cv::Mat resized;
    cv::resize(bgr, resized, scaled, 0, 0, scale < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR);
    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

    // 1×3×1024×1024，RGB、0～1，右邊和下面補 0
    const std::size_t plane = static_cast<std::size_t>(kInputSize) * kInputSize;
    std::vector<float> input(plane * 3, 0.0f);
    for (int y = 0; y < rgb.rows; ++y) {
        const std::uint8_t* row = rgb.ptr<std::uint8_t>(y);
        for (int x = 0; x < rgb.cols; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * kInputSize + x;
            for (int channel = 0; channel < 3; ++channel) {
                input[plane * channel + index] = row[x * 3 + channel] / 255.0f;
            }
        }
    }
    const std::array<std::int64_t, 4> shape{1, 3, kInputSize, kInputSize};
    const Tensor output = model_.run(input, shape);
    return decodeComicTextBlocks(output.data, scale, {bgr.cols, bgr.rows}, options_);
}

}  // namespace tmw::ocr
