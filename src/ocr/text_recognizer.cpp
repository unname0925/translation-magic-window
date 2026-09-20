#include "ocr/text_recognizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <opencv2/imgproc.hpp>
#include <ranges>
#include <stdexcept>

namespace tmw::ocr {
namespace {

// PaddleX 的 OCRReisizeNormImg.max_imgW
constexpr int kMaxInputWidth = 3200;

// 批次辨識的寬度級距。DirectML 每遇到一種新的輸入大小就要重新編譯，所以級距要少；
// 級距之間又不能差太多，否則補的 0 太多、浪費算力。
constexpr std::array<int, 8> kWidthBuckets{160, 256, 384, 512, 768, 1024, 1600, kMaxInputWidth};

// numpy 的 np.linalg.norm（float32）：在 float32 下計算
float distance(const cv::Point2f& a, const cv::Point2f& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// 把裁切圖縮放、正規化後寫進 input 的第 index 張（NCHW，右邊和多出來的部分留 0）。
void writeInput(const cv::Mat& crop, int imageHeight, const RecognitionWidth& width,
                int paddedWidth, std::size_t index, std::vector<float>& input) {
    cv::Mat resized;
    cv::resize(crop, resized, cv::Size(width.resizedWidth, imageHeight));
    const std::size_t image = index * 3 * static_cast<std::size_t>(imageHeight) * paddedWidth;
    for (int y = 0; y < imageHeight; ++y) {
        const std::uint8_t* row = resized.ptr<std::uint8_t>(y);
        for (int x = 0; x < width.resizedWidth; ++x) {
            for (int c = 0; c < 3; ++c) {
                // (x / 255 - 0.5) / 0.5，都在 float32 下計算
                float value = static_cast<float>(row[x * 3 + c]) / 255.0f;
                value -= 0.5f;
                value /= 0.5f;
                input[image + (static_cast<std::size_t>(c) * imageHeight + y) * paddedWidth + x] =
                    value;
            }
        }
    }
}

}  // namespace

int recognitionWidthBucket(int paddedWidth) {
    for (const int bucket : kWidthBuckets) {
        if (paddedWidth <= bucket) {
            return bucket;
        }
    }
    return kMaxInputWidth;
}

RecognitionWidth recognitionInputWidth(int cropHeight, int cropWidth, int imageHeight,
                                       int minimumWidth) {
    if (cropHeight <= 0 || cropWidth <= 0) {
        throw std::invalid_argument("recognitionInputWidth: empty crop");
    }
    const double ratio = cropWidth * 1.0 / cropHeight;
    const double maxRatio = std::max(static_cast<double>(minimumWidth) / imageHeight, ratio);
    int paddedWidth = static_cast<int>(imageHeight * maxRatio);
    if (paddedWidth > kMaxInputWidth) {
        return {kMaxInputWidth, kMaxInputWidth};
    }
    const double wanted = std::ceil(imageHeight * ratio);
    const int resizedWidth = wanted > paddedWidth ? paddedWidth : static_cast<int>(wanted);
    return {resizedWidth, paddedWidth};
}

Recognition ctcGreedyDecode(std::span<const float> probabilities, int timeSteps,
                            const std::vector<std::string>& characters) {
    const size_t classes = characters.size();
    if (classes == 0 || probabilities.size() != static_cast<size_t>(timeSteps) * classes) {
        throw std::invalid_argument("ctcGreedyDecode: size mismatch");
    }
    Recognition result;
    double scoreSum = 0.0;
    int selected = 0;
    size_t previous = 0;
    for (int t = 0; t < timeSteps; ++t) {
        const float* row = probabilities.data() + static_cast<size_t>(t) * classes;
        // np.argmax：相同的最大值取第一個
        const size_t index = static_cast<size_t>(std::max_element(row, row + classes) - row);
        const bool repeated = t > 0 && index == previous;
        previous = index;
        if (repeated || index == 0) {
            continue;
        }
        result.text += characters[index];
        scoreSum += row[index];
        ++selected;
    }
    result.score = selected > 0 ? static_cast<float>(scoreSum / selected) : 0.0f;
    return result;
}

cv::Mat cropTextRegion(const cv::Mat& bgr, const Quad& box) {
    // get_minarea_rect_crop：先取最小外接矩形，四個角排成左上、右上、右下、左下
    const std::vector<cv::Point> points(box.begin(), box.end());
    const MiniBox rect = miniBox(cv::minAreaRect(points));
    const std::array<cv::Point2f, 4>& p = rect.points;

    // get_rotate_crop_image
    const int width = static_cast<int>(std::max(distance(p[0], p[1]), distance(p[2], p[3])));
    const int height = static_cast<int>(std::max(distance(p[0], p[3]), distance(p[1], p[2])));
    if (width <= 0 || height <= 0) {
        return {};
    }
    const std::array<cv::Point2f, 4> target{
        cv::Point2f(0.0f, 0.0f), cv::Point2f(static_cast<float>(width), 0.0f),
        cv::Point2f(static_cast<float>(width), static_cast<float>(height)),
        cv::Point2f(0.0f, static_cast<float>(height))};
    const cv::Mat transform = cv::getPerspectiveTransform(p.data(), target.data());
    cv::Mat crop;
    cv::warpPerspective(bgr, crop, transform, cv::Size(width, height), cv::INTER_CUBIC,
                        cv::BORDER_REPLICATE);
    if (crop.rows * 1.0 / crop.cols >= 1.5) {
        cv::Mat rotated;
        cv::rotate(crop, rotated, cv::ROTATE_90_COUNTERCLOCKWISE);  // np.rot90
        return rotated;
    }
    return crop;
}

TextRecognizer::TextRecognizer(const std::filesystem::path& modelDir, Device device)
    : config_(loadRecognitionModelConfig(modelDir / "inference.yml")),
      model_(modelDir / "inference.onnx", device) {}

Recognition TextRecognizer::recognize(const cv::Mat& crop) {
    CV_Assert(crop.type() == CV_8UC3 && !crop.empty());
    const int imageHeight = config_.imageHeight;
    const RecognitionWidth width =
        recognitionInputWidth(crop.rows, crop.cols, imageHeight, config_.imageWidth);
    const int paddedWidth = width.paddedWidth;
    std::vector<float> input(static_cast<std::size_t>(3) * imageHeight * paddedWidth, 0.0f);
    writeInput(crop, imageHeight, width, paddedWidth, 0, input);

    const std::array<std::int64_t, 4> shape{1, 3, imageHeight, paddedWidth};
    const Tensor output = model_.run(input, shape);
    if (output.shape.size() != 3 || output.shape[0] != 1 ||
        output.shape[2] != static_cast<std::int64_t>(config_.characters.size())) {
        throw std::runtime_error("recognition output does not match the character dictionary (" +
                                 std::to_string(config_.characters.size()) + " characters)");
    }
    return ctcGreedyDecode(output.data, static_cast<int>(output.shape[1]), config_.characters);
}

std::vector<Recognition> TextRecognizer::recognize(std::span<const cv::Mat> crops, int maxBatch) {
    const int imageHeight = config_.imageHeight;
    const std::size_t classes = config_.characters.size();
    std::vector<Recognition> results(crops.size());

    // 先算每張要補到多寬，再依級距排序，寬度相近的才會落在同一批
    struct Item {
        std::size_t index = 0;
        RecognitionWidth width;
        int bucket = 0;
    };
    std::vector<Item> items;
    items.reserve(crops.size());
    for (std::size_t i = 0; i < crops.size(); ++i) {
        const cv::Mat& crop = crops[i];
        if (crop.empty()) {
            continue;  // 裁不出來的框留空結果
        }
        CV_Assert(crop.type() == CV_8UC3);
        const RecognitionWidth width =
            recognitionInputWidth(crop.rows, crop.cols, imageHeight, config_.imageWidth);
        items.push_back({i, width, recognitionWidthBucket(width.paddedWidth)});
    }
    std::ranges::stable_sort(items, {}, &Item::bucket);

    std::vector<float> input;
    for (std::size_t start = 0; start < items.size();) {
        const int bucket = items[start].bucket;
        std::size_t end = start;
        while (end < items.size() && items[end].bucket == bucket &&
               end - start < static_cast<std::size_t>(std::max(1, maxBatch))) {
            ++end;
        }
        const std::size_t count = end - start;
        input.assign(count * 3 * static_cast<std::size_t>(imageHeight) * bucket, 0.0f);
        for (std::size_t i = start; i < end; ++i) {
            writeInput(crops[items[i].index], imageHeight, items[i].width, bucket, i - start,
                       input);
        }

        const std::array<std::int64_t, 4> shape{static_cast<std::int64_t>(count), 3, imageHeight,
                                                bucket};
        const Tensor output = model_.run(input, shape);
        if (output.shape.size() != 3 || output.shape[0] != static_cast<std::int64_t>(count) ||
            output.shape[2] != static_cast<std::int64_t>(classes)) {
            throw std::runtime_error(
                "recognition output does not match the character dictionary (" +
                std::to_string(classes) + " characters)");
        }
        const auto timeSteps = static_cast<std::size_t>(output.shape[1]);
        for (std::size_t i = start; i < end; ++i) {
            const std::span<const float> probabilities(
                output.data.data() + (i - start) * timeSteps * classes, timeSteps * classes);
            results[items[i].index] =
                ctcGreedyDecode(probabilities, static_cast<int>(timeSteps), config_.characters);
        }
        start = end;
    }
    return results;
}

}  // namespace tmw::ocr
