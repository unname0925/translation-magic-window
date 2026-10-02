#include "ocr/lama_inpainter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <utility>
#include <vector>

#include "ocr/onnx_session.h"

namespace tmw::ocr {

core::RectI lamaCrop(core::SizeI frame, const core::RectI& rect, int margin) {
    const int side = std::max(rect.width(), rect.height()) + 2 * margin;
    if (side <= frame.width && side <= frame.height) {
        // 正方形，中心對準 rect，碰到畫面邊就往內推
        const int centerX = (rect.left + rect.right) / 2;
        const int centerY = (rect.top + rect.bottom) / 2;
        const int left = std::clamp(centerX - side / 2, 0, frame.width - side);
        const int top = std::clamp(centerY - side / 2, 0, frame.height - side);
        return {left, top, left + side, top + side};
    }
    return {std::max(0, rect.left - margin), std::max(0, rect.top - margin),
            std::min(frame.width, rect.right + margin),
            std::min(frame.height, rect.bottom + margin)};
}

LamaInpainter::LamaInpainter(std::filesystem::path model) : model_(std::move(model)) {}

LamaInpainter::~LamaInpainter() = default;

std::filesystem::path LamaInpainter::modelPath(const std::filesystem::path& modelsDirectory) {
    return modelsDirectory / L"lama" / L"lama_fp32_dml.onnx";
}

bool LamaInpainter::ensureLoaded() {
    if (session_ != nullptr) {
        return true;
    }
    if (failed_) {
        return false;
    }
    try {
        if (!std::filesystem::exists(model_)) {
            throw std::runtime_error("找不到 " + model_.string());
        }
        // 明確指定 DirectML：建立失敗就是失敗，不默默改用 CPU（CPU 上太慢）
        session_ = std::make_unique<OnnxSession>(model_, Device::DirectML);
        return true;
    } catch (const std::exception& error) {
        failed_ = true;
        problem_ = error.what();
        return false;
    }
}

std::optional<core::ImageBgra> LamaInpainter::inpaint(const core::ImageBgra& frame,
                                                      const core::RectI& rect) {
    const core::RectI area = core::intersect(rect, core::RectI{0, 0, frame.width, frame.height});
    if (frame.empty() || area.empty() || !ensureLoaded()) {
        return std::nullopt;
    }
    const core::RectI crop = lamaCrop(core::SizeI{frame.width, frame.height}, area);
    const cv::Mat whole(frame.height, frame.width, CV_8UC4,
                        const_cast<std::uint8_t*>(frame.pixels.data()), frame.stride());
    const cv::Mat region = whole(cv::Rect(crop.left, crop.top, crop.width(), crop.height()));

    // 輸入：RGB、0～1、CHW
    cv::Mat rgb;
    cv::cvtColor(region, rgb, cv::COLOR_BGRA2RGB);
    cv::Mat resized;
    const bool shrinking = crop.width() > kInputSize || crop.height() > kInputSize;
    cv::resize(rgb, resized, cv::Size(kInputSize, kInputSize), 0, 0,
               shrinking ? cv::INTER_AREA : cv::INTER_LINEAR);
    const std::size_t plane = static_cast<std::size_t>(kInputSize) * kInputSize;
    std::vector<float> image(plane * 3);
    for (int y = 0; y < kInputSize; ++y) {
        const std::uint8_t* row = resized.ptr<std::uint8_t>(y);
        for (int x = 0; x < kInputSize; ++x) {
            for (std::size_t c = 0; c < 3; ++c) {
                image[c * plane + static_cast<std::size_t>(y) * kInputSize + x] =
                    row[x * 3 + static_cast<int>(c)] / 255.0f;
            }
        }
    }
    // 遮罩：要抹掉的範圍是 1（縮放後往外取整，寧可多抹一點也不要留下字的邊）
    std::vector<float> mask(plane, 0.0f);
    const double scaleX = static_cast<double>(kInputSize) / crop.width();
    const double scaleY = static_cast<double>(kInputSize) / crop.height();
    const int maskLeft =
        std::max(0, static_cast<int>(std::floor((area.left - crop.left) * scaleX)));
    const int maskTop = std::max(0, static_cast<int>(std::floor((area.top - crop.top) * scaleY)));
    const int maskRight =
        std::min(kInputSize, static_cast<int>(std::ceil((area.right - crop.left) * scaleX)));
    const int maskBottom =
        std::min(kInputSize, static_cast<int>(std::ceil((area.bottom - crop.top) * scaleY)));
    for (int y = maskTop; y < maskBottom; ++y) {
        std::fill_n(mask.begin() + static_cast<std::ptrdiff_t>(y) * kInputSize + maskLeft,
                    maskRight - maskLeft, 1.0f);
    }

    std::vector<Tensor> outputs;
    try {
        const std::array<std::int64_t, 4> imageShape{1, 3, kInputSize, kInputSize};
        const std::array<std::int64_t, 4> maskShape{1, 1, kInputSize, kInputSize};
        const std::array<NamedInput, 2> inputs{NamedInput::ofFloats("image", imageShape, image),
                                               NamedInput::ofFloats("mask", maskShape, mask)};
        const std::array<const char*, 1> names{"output"};
        outputs = session_->run(inputs, names);
    } catch (const std::exception& error) {
        problem_ = error.what();
        return std::nullopt;
    }
    if (outputs.empty() || outputs[0].data.size() != plane * 3) {
        return std::nullopt;
    }

    // 輸出：RGB、0～255、CHW → 縮回 crop 的大小 → 取 area 那一塊
    cv::Mat result(kInputSize, kInputSize, CV_8UC3);
    const std::vector<float>& out = outputs[0].data;
    for (int y = 0; y < kInputSize; ++y) {
        std::uint8_t* row = result.ptr<std::uint8_t>(y);
        for (int x = 0; x < kInputSize; ++x) {
            const std::size_t at = static_cast<std::size_t>(y) * kInputSize + x;
            for (std::size_t c = 0; c < 3; ++c) {
                // OpenCV 是 BGR：c = 0（R）放到第 2 個通道
                row[x * 3 + 2 - static_cast<int>(c)] =
                    cv::saturate_cast<std::uint8_t>(out[c * plane + at]);
            }
        }
    }
    cv::Mat back;
    cv::resize(result, back, cv::Size(crop.width(), crop.height()), 0, 0, cv::INTER_LINEAR);
    const cv::Mat piece =
        back(cv::Rect(area.left - crop.left, area.top - crop.top, area.width(), area.height()));

    core::ImageBgra patch(area.width(), area.height());
    cv::Mat target(patch.height, patch.width, CV_8UC4, patch.pixels.data(), patch.stride());
    cv::cvtColor(piece, target, cv::COLOR_BGR2BGRA);
    return patch;
}

}  // namespace tmw::ocr
