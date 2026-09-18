#include "core/lens_layout.h"

#include <algorithm>
#include <stdexcept>

namespace tmw::core {
namespace {

// 抓取區的像素：幾乎完全透明，但 alpha 不為 0，所以滑鼠點得到。
constexpr std::uint32_t kGrabPixel = toPremultipliedArgb({0, 0, 0, 1});
constexpr std::uint32_t kGripDotPixel = toPremultipliedArgb({255, 255, 255, 220});

// 把手中央 3×2 個小圓點（點和間距都是 gripDot 大小）。
bool isGripDot(const LensLayout& layout, PointI point) {
    constexpr int kColumns = 3;
    constexpr int kRows = 2;
    const int dot = layout.gripDot;
    if (dot <= 0) {
        return false;
    }
    const int gridWidth = (2 * kColumns - 1) * dot;
    const int gridHeight = (2 * kRows - 1) * dot;
    const int left = layout.handle.left + (layout.handle.width() - gridWidth) / 2;
    const int top = layout.handle.top + (layout.handle.height() - gridHeight) / 2;
    const int x = point.x - left;
    const int y = point.y - top;
    if (x < 0 || y < 0 || x >= gridWidth || y >= gridHeight) {
        return false;
    }
    return (x / dot) % 2 == 0 && (y / dot) % 2 == 0;
}

}  // namespace

LensMetrics scaledLensMetrics(unsigned dpi) {
    const LensMetrics base;
    const auto scale = [dpi](int value) { return std::max(1, scaleForDpi(value, dpi)); };
    LensMetrics scaled;
    scaled.outerGrab = scale(base.outerGrab);
    scaled.border = scale(base.border);
    scaled.innerGrab = scale(base.innerGrab);
    scaled.handleWidth = scale(base.handleWidth);
    scaled.handleHeight = scale(base.handleHeight);
    scaled.corner = scale(base.corner);
    scaled.gripDot = scale(base.gripDot);
    scaled.minContentWidth = scale(base.minContentWidth);
    scaled.minContentHeight = scale(base.minContentHeight);
    return scaled;
}

LensLayout computeLensLayout(SizeI window, unsigned dpi) {
    const LensMetrics metrics = scaledLensMetrics(dpi);
    LensLayout layout;
    layout.window = window;
    layout.cornerSize = metrics.corner;
    layout.gripDot = metrics.gripDot;
    // 把手的下緣剛好貼齊可見邊框的上緣
    layout.frame = {0, metrics.handleHeight - metrics.outerGrab, window.width, window.height};
    layout.border = inflate(layout.frame, -metrics.outerGrab);
    layout.content = inflate(layout.border, -metrics.border);
    layout.clickThrough = inflate(layout.content, -metrics.innerGrab);
    if (!layout.border.empty()) {
        layout.handle = {layout.border.left, 0,
                         std::min(layout.border.left + metrics.handleWidth, layout.border.right),
                         layout.border.top};
    }
    return layout;
}

SizeI lensWindowSizeForContent(SizeI content, unsigned dpi) {
    const LensMetrics metrics = scaledLensMetrics(dpi);
    const int side = metrics.outerGrab + metrics.border;
    // 高度：上方是把手（它已經蓋過外側抓取區），下方是外側抓取區加邊框
    return {content.width + 2 * side,
            metrics.handleHeight + metrics.border + content.height + side};
}

SizeI minimumLensWindowSize(unsigned dpi) {
    const LensMetrics metrics = scaledLensMetrics(dpi);
    return lensWindowSizeForContent({metrics.minContentWidth, metrics.minContentHeight}, dpi);
}

LensHitZone hitTestLens(const LensLayout& layout, PointI point) {
    if (layout.handle.contains(point)) {
        return LensHitZone::Move;
    }
    if (!layout.frame.contains(point) || layout.clickThrough.contains(point)) {
        return LensHitZone::Transparent;
    }

    const RectI& frame = layout.frame;
    const int fromLeft = point.x - frame.left;
    const int fromRight = frame.right - 1 - point.x;
    const int fromTop = point.y - frame.top;
    const int fromBottom = frame.bottom - 1 - point.y;

    const bool nearLeft = fromLeft < layout.cornerSize;
    const bool nearRight = fromRight < layout.cornerSize;
    const bool nearTop = fromTop < layout.cornerSize;
    const bool nearBottom = fromBottom < layout.cornerSize;
    if (nearTop && nearLeft) {
        return LensHitZone::TopLeft;
    }
    if (nearTop && nearRight) {
        return LensHitZone::TopRight;
    }
    if (nearBottom && nearLeft) {
        return LensHitZone::BottomLeft;
    }
    if (nearBottom && nearRight) {
        return LensHitZone::BottomRight;
    }

    const int nearest = std::min({fromLeft, fromRight, fromTop, fromBottom});
    if (nearest == fromLeft) {
        return LensHitZone::Left;
    }
    if (nearest == fromRight) {
        return LensHitZone::Right;
    }
    if (nearest == fromTop) {
        return LensHitZone::Top;
    }
    return LensHitZone::Bottom;
}

void renderLens(const LensLayout& layout, Rgba accent, std::span<std::uint32_t> pixels) {
    const int width = layout.window.width;
    const int height = layout.window.height;
    if (width < 0 || height < 0 ||
        pixels.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) {
        throw std::invalid_argument("renderLens: pixel buffer size does not match the window");
    }

    std::fill(pixels.begin(), pixels.end(), 0u);
    const std::uint32_t accentPixel = toPremultipliedArgb(accent);

    const auto paint = [&](int x, int y) {
        const PointI point{x, y};
        const LensHitZone zone = hitTestLens(layout, point);
        if (zone == LensHitZone::Transparent) {
            return;
        }
        std::uint32_t pixel = kGrabPixel;
        if (zone == LensHitZone::Move) {
            pixel = isGripDot(layout, point) ? kGripDotPixel : accentPixel;
        } else if (layout.border.contains(point) && !layout.content.contains(point)) {
            pixel = accentPixel;
        }
        pixels[static_cast<size_t>(y) * width + x] = pixel;
    };

    // 中間的穿透區一定是透明的，跳過它，繪製時間只和周長成正比。
    const RectI& hole = layout.clickThrough;
    for (int y = 0; y < height; ++y) {
        const bool crossesHole = !hole.empty() && y >= hole.top && y < hole.bottom;
        if (!crossesHole) {
            for (int x = 0; x < width; ++x) {
                paint(x, y);
            }
            continue;
        }
        for (int x = 0; x < hole.left; ++x) {
            paint(x, y);
        }
        for (int x = hole.right; x < width; ++x) {
            paint(x, y);
        }
    }
}

}  // namespace tmw::core
