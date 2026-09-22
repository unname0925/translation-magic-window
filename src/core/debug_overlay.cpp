#include "core/debug_overlay.h"

#include <algorithm>
#include <stdexcept>
#include <string>

#include "core/language.h"

namespace tmw::core {
namespace {

std::string roundedMs(double milliseconds) {
    return std::to_string(static_cast<int>(milliseconds + 0.5)) + " ms";
}

std::uint32_t premultiplied(Rgba color) {
    const auto scale = [&color](std::uint8_t channel) {
        return static_cast<std::uint32_t>(channel * color.a / 255);
    };
    return (static_cast<std::uint32_t>(color.a) << 24) | (scale(color.r) << 16) |
           (scale(color.g) << 8) | scale(color.b);
}

// 把 [left, right) × [top, bottom) 填滿，超出畫面的部分裁掉
void fill(std::span<std::uint32_t> pixels, SizeI size, int left, int top, int right, int bottom,
          std::uint32_t color) {
    left = std::max(left, 0);
    top = std::max(top, 0);
    right = std::min(right, size.width);
    bottom = std::min(bottom, size.height);
    for (int y = top; y < bottom; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * size.width;
        for (int x = left; x < right; ++x) {
            pixels[row + static_cast<std::size_t>(x)] = color;
        }
    }
}

}  // namespace

DebugOverlay buildDebugOverlay(const std::string& stateName) {
    DebugOverlay overlay;
    overlay.status.push_back("狀態：" + stateName);
    overlay.status.push_back("（還沒處理過）");
    return overlay;
}

DebugOverlay buildDebugOverlay(const PipelineResult& result, const RectI& overlayScreenRect,
                               const std::string& stateName) {
    DebugOverlay overlay;
    overlay.status.push_back("狀態：" + stateName);

    // 處理完之後透鏡可能被移動過：框要畫在它「當時」對應的位置上
    const int dx = result.region.left - overlayScreenRect.left;
    const int dy = result.region.top - overlayScreenRect.top;
    const auto place = [dx, dy](const RectI& rect) {
        return RectI{rect.left + dx, rect.top + dy, rect.right + dx, rect.bottom + dy};
    };

    for (const OcrLine& line : result.lines) {
        overlay.boxes.push_back({place(line.rect), kOverlayLineColor, 1});
    }
    for (const TranslatedBlock& group : result.groups) {
        overlay.boxes.push_back({place(group.block.rect), kOverlayBlockColor, 2});
    }

    overlay.status.push_back(std::to_string(result.lines.size()) + " 行 → " +
                             std::to_string(result.groups.size()) + " 段（" +
                             languageCode(result.language) + "）");
    overlay.status.push_back("OCR " + roundedMs(result.timings.ocrMs) + "／分段 " +
                             roundedMs(result.timings.layoutMs) + "／翻譯 " +
                             roundedMs(result.timings.translationMs));
    overlay.status.push_back("共 " + roundedMs(result.timings.totalMs()));
    if (result.unchanged) {
        overlay.status.push_back("和上一次一樣");
    }
    if (!result.error.empty()) {
        overlay.status.push_back("翻譯失敗：" + result.error);
    }
    return overlay;
}

void renderDebugOverlayBoxes(const DebugOverlay& overlay, SizeI size,
                             std::span<std::uint32_t> pixels) {
    const std::size_t expected =
        static_cast<std::size_t>(std::max(size.width, 0)) * std::max(size.height, 0);
    if (pixels.size() != expected) {
        throw std::invalid_argument("renderDebugOverlayBoxes：pixels 的大小和 size 不符");
    }
    std::ranges::fill(pixels, 0u);

    for (const OverlayBox& box : overlay.boxes) {
        const std::uint32_t color = premultiplied(box.color);
        const int thickness = std::max(box.thickness, 1);
        // 四條邊，往框的內側長
        fill(pixels, size, box.rect.left, box.rect.top, box.rect.right, box.rect.top + thickness,
             color);
        fill(pixels, size, box.rect.left, box.rect.bottom - thickness, box.rect.right,
             box.rect.bottom, color);
        fill(pixels, size, box.rect.left, box.rect.top, box.rect.left + thickness, box.rect.bottom,
             color);
        fill(pixels, size, box.rect.right - thickness, box.rect.top, box.rect.right,
             box.rect.bottom, color);
    }
}

}  // namespace tmw::core
