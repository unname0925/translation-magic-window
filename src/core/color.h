#pragma once

#include <cstdint>

namespace tmw::core {

struct Rgba {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    friend constexpr bool operator==(const Rgba&, const Rgba&) = default;
};

// 轉成「預乘 alpha」的 0xAARRGGBB，這是 Windows 分層視窗（UpdateLayeredWindow）要求的格式：
// RGB 各分量都要先乘上 alpha / 255。
constexpr std::uint32_t toPremultipliedArgb(Rgba color) {
    const auto premultiply = [alpha = color.a](std::uint8_t value) -> std::uint32_t {
        return (static_cast<std::uint32_t>(value) * alpha + 127) / 255;
    };
    return (static_cast<std::uint32_t>(color.a) << 24) | (premultiply(color.r) << 16) |
           (premultiply(color.g) << 8) | premultiply(color.b);
}

constexpr std::uint8_t alphaOf(std::uint32_t argb) {
    return static_cast<std::uint8_t>(argb >> 24);
}

}  // namespace tmw::core
