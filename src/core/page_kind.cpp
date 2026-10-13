#include "core/page_kind.h"

#include <algorithm>
#include <cstdint>

#include "core/utf8.h"

namespace tmw::core {
namespace {

constexpr int kMinimumCharacters = 200;
constexpr int kMinimumSegments = 15;
constexpr double kHorizontalShare = 0.5;
constexpr double kColorShare = 0.3;
constexpr double kBubbleShare = 0.5;

// HSV 的飽和度 > 80/255、亮度 > 40/255 算彩色；每 4×4 取一點就夠
double colorShare(const ImageBgra& frame) {
    int sampled = 0;
    int colored = 0;
    for (int y = 0; y < frame.height; y += 4) {
        for (int x = 0; x < frame.width; x += 4) {
            const std::uint8_t* p = frame.pixel(x, y);
            const int high = std::max({p[0], p[1], p[2]});
            const int low = std::min({p[0], p[1], p[2]});
            ++sampled;
            if (high > 40 && (high - low) * 255 > 80 * high) {
                ++colored;
            }
        }
    }
    return sampled == 0 ? 0.0 : static_cast<double>(colored) / sampled;
}

}  // namespace

PageTextStats pageTextStats(const ImageBgra& frame, std::span<const TextBlock> blocks) {
    PageTextStats stats;
    int horizontal = 0;
    int inBubble = 0;
    for (const TextBlock& block : blocks) {
        const int characters = characterCount(block.text);
        ++stats.segments;
        stats.characters += characters;
        horizontal += block.orientation == Orientation::Horizontal ? characters : 0;
        inBubble += block.inBubble ? characters : 0;
    }
    if (stats.characters > 0) {
        stats.horizontalShare = static_cast<double>(horizontal) / stats.characters;
        stats.bubbleShare = static_cast<double>(inBubble) / stats.characters;
    }
    // 字少的頁不必算顏色（一定不是廣告）
    if (stats.characters >= kMinimumCharacters && stats.segments >= kMinimumSegments) {
        stats.colorShare = colorShare(frame);
    }
    return stats;
}

bool looksLikeAdPage(const PageTextStats& stats) {
    return stats.characters >= kMinimumCharacters && stats.segments >= kMinimumSegments &&
           (stats.horizontalShare > kHorizontalShare || stats.colorShare >= kColorShare ||
            stats.bubbleShare < kBubbleShare);
}

}  // namespace tmw::core
