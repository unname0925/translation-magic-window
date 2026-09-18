#include "core/geometry.h"

#include <algorithm>
#include <cmath>

namespace tmw::core {

RectI intersect(const RectI& a, const RectI& b) {
    const RectI result{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right),
                       std::min(a.bottom, b.bottom)};
    return result.empty() ? RectI{} : result;
}

RectI offset(const RectI& rect, int dx, int dy) {
    return {rect.left + dx, rect.top + dy, rect.right + dx, rect.bottom + dy};
}

RectI inflate(const RectI& rect, int amount) {
    const RectI result{rect.left - amount, rect.top - amount, rect.right + amount,
                       rect.bottom + amount};
    return result.empty() ? RectI{} : result;
}

RectI screenToMonitorLocal(const RectI& screenRect, const RectI& monitorRect) {
    const RectI clipped = intersect(screenRect, monitorRect);
    if (clipped.empty()) {
        return {};
    }
    return offset(clipped, -monitorRect.left, -monitorRect.top);
}

int scaleForDpi(int value, unsigned dpi) {
    return static_cast<int>(std::lround(static_cast<double>(value) * dpi / 96.0));
}

}  // namespace tmw::core
