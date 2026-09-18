#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tmw::core {

// 32 位元 BGRA 影像，每列緊密排列（一列 width * 4 bytes，沒有補齊）。
struct ImageBgra {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    ImageBgra() = default;
    ImageBgra(int w, int h)
        : width(w),
          height(h),
          pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4) {}

    bool empty() const { return width <= 0 || height <= 0; }
    std::size_t stride() const { return static_cast<std::size_t>(width) * 4; }

    std::uint8_t* pixel(int x, int y) {
        return pixels.data() + static_cast<std::size_t>(y) * stride() +
               static_cast<std::size_t>(x) * 4;
    }
    const std::uint8_t* pixel(int x, int y) const {
        return pixels.data() + static_cast<std::size_t>(y) * stride() +
               static_cast<std::size_t>(x) * 4;
    }
};

// 8 位元灰階影像（例如變化偵測用的縮圖），每列緊密排列。
struct GrayImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    GrayImage() = default;
    GrayImage(int w, int h, std::uint8_t value = 0)
        : width(w),
          height(h),
          pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), value) {}

    bool empty() const { return width <= 0 || height <= 0; }

    std::uint8_t& at(int x, int y) {
        return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                      static_cast<std::size_t>(x)];
    }
    std::uint8_t at(int x, int y) const {
        return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                      static_cast<std::size_t>(x)];
    }

    friend bool operator==(const GrayImage&, const GrayImage&) = default;
};

}  // namespace tmw::core
