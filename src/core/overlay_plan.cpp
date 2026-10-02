#include "core/overlay_plan.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "core/ruby.h"

namespace tmw::core {
namespace {

// 原文框往外多蓋這麼多像素：OCR 的框常常貼著字，反鋸齒的邊緣會露出來
constexpr int kPadding = 3;
// 哪些段落要蓋。用純色蓋在畫上會是一塊突兀的色塊，而且擬聲詞本來就不該蓋（design.md 4.8），
// 這些留著原文，譯文在結果視窗裡。3 頁日文漫畫、3 張日文遊戲截圖上量的：
// - 外圍至少 80% 和背景色差不到 24：漫畫對話框是 0.85～1.00，擬聲詞和畫在圖上的字 0.03～0.76
constexpr double kMinUniformity = 0.8;
// - 遊戲的對話框是半透明的，透出底下的畫面，只有 0.72～0.84；放寬到差 72 以內是 0.90～0.95。
//   擬聲詞放寬後也有到 0.86～0.90 的，但 OCR 分數都在 0.6 以下（對話是 0.82 以上），
//   所以放寬的這條要 OCR 很有把握才算
constexpr int kLooseTolerance = 72;
constexpr double kMinLooseUniformity = 0.9;
constexpr float kMinScoreForLoose = 0.9f;
// - OCR 分數這麼低的多半是把圖示、花紋讀成了字（遊戲的按鈕讀成「迴」），不蓋
constexpr float kMinScore = 0.5f;

std::uint8_t median(std::vector<std::uint8_t>& values) {
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

int lineThickness(const TextBlock& block) {
    std::vector<int> sizes;
    sizes.reserve(block.lines.size());
    for (const OcrLine& line : block.lines) {
        sizes.push_back(line.orientation == Orientation::Vertical ? line.rect.width()
                                                                  : line.rect.height());
    }
    if (sizes.empty()) {
        return 0;
    }
    const auto middle = sizes.begin() + static_cast<std::ptrdiff_t>(sizes.size() / 2);
    std::nth_element(sizes.begin(), middle, sizes.end());
    return *middle;
}

}  // namespace

Rgba sampleBackground(const ImageBgra& frame, const RectI& rect, int margin) {
    if (frame.empty()) {
        return Rgba{255, 255, 255, 255};
    }
    std::vector<std::uint8_t> blue;
    std::vector<std::uint8_t> green;
    std::vector<std::uint8_t> red;
    const auto take = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) {
            return;
        }
        const std::uint8_t* p = frame.pixel(x, y);
        blue.push_back(p[0]);
        green.push_back(p[1]);
        red.push_back(p[2]);
    };
    const int left = rect.left - margin;
    const int top = rect.top - margin;
    const int right = rect.right + margin - 1;
    const int bottom = rect.bottom + margin - 1;
    for (int x = left; x <= right; ++x) {
        take(x, top);
        take(x, bottom);
    }
    for (int y = top + 1; y < bottom; ++y) {
        take(left, y);
        take(right, y);
    }
    if (blue.empty()) {
        return Rgba{255, 255, 255, 255};
    }
    return Rgba{median(red), median(green), median(blue), 255};
}

double backgroundUniformity(const ImageBgra& frame, const RectI& rect, Rgba background, int margin,
                            int tolerance) {
    int total = 0;
    int close = 0;
    const auto take = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) {
            return;
        }
        const std::uint8_t* p = frame.pixel(x, y);
        ++total;
        if (std::abs(p[0] - background.b) <= tolerance &&
            std::abs(p[1] - background.g) <= tolerance &&
            std::abs(p[2] - background.r) <= tolerance) {
            ++close;
        }
    };
    const int left = rect.left - margin;
    const int top = rect.top - margin;
    const int right = rect.right + margin - 1;
    const int bottom = rect.bottom + margin - 1;
    for (int x = left; x <= right; ++x) {
        take(x, top);
        take(x, bottom);
    }
    for (int y = top + 1; y < bottom; ++y) {
        take(left, y);
        take(right, y);
    }
    return total == 0 ? 1.0 : static_cast<double>(close) / total;
}

Rgba readableTextColor(Rgba background) {
    // BT.601 亮度
    const int luminance = (77 * background.r + 150 * background.g + 29 * background.b) >> 8;
    return luminance >= 128 ? Rgba{0, 0, 0, 255} : Rgba{255, 255, 255, 255};
}

std::string overlayText(std::string_view translation) {
    std::string out(stripRubyMarkup(translation));
    if (out.size() == translation.size()) {
        return out;  // 沒有標記
    }
    out.clear();
    std::size_t at = 0;
    for (const auto& [base, reading] : rubyMarkupPairs(translation)) {
        const std::string marked = "{" + base + "|" + reading + "}";
        const std::size_t found = translation.find(marked, at);
        if (found == std::string_view::npos) {
            break;
        }
        out.append(translation.substr(at, found - at));
        out += base + "（" + reading + "）";
        at = found + marked.size();
    }
    out.append(translation.substr(at));
    return out;
}

void compositeOver(ImageBgra& below, const ImageBgra& above) {
    if (below.width != above.width || below.height != above.height) {
        return;
    }
    for (std::size_t i = 0; i + 3 < below.pixels.size(); i += 4) {
        const int alpha = above.pixels[i + 3];
        for (std::size_t channel = 0; channel < 3; ++channel) {
            // 預乘過的顏色：結果 = 上 + 下 × (1 − α)
            const int mixed =
                above.pixels[i + channel] + (below.pixels[i + channel] * (255 - alpha) + 127) / 255;
            below.pixels[i + channel] = static_cast<std::uint8_t>(std::min(mixed, 255));
        }
        below.pixels[i + 3] = 255;
    }
}

bool worthCovering(const ImageBgra& frame, const RectI& rect, Rgba background, float score) {
    if (score < kMinScore) {
        return false;
    }
    if (backgroundUniformity(frame, rect, background) >= kMinUniformity) {
        return true;
    }
    return score >= kMinScoreForLoose &&
           backgroundUniformity(frame, rect, background, 2, kLooseTolerance) >= kMinLooseUniformity;
}

std::vector<OverlayItem> planOverlay(const ImageBgra& frame,
                                     std::span<const TranslatedBlock> groups) {
    std::vector<OverlayItem> items;
    for (const TranslatedBlock& group : groups) {
        if (group.translation.empty()) {
            continue;  // 翻譯失敗：不蓋，原文照樣看得到
        }
        OverlayItem item;
        const RectI& source = group.block.rect;
        item.rect = RectI{std::max(0, source.left - kPadding), std::max(0, source.top - kPadding),
                          std::min(frame.width, source.right + kPadding),
                          std::min(frame.height, source.bottom + kPadding)};
        item.text = overlayText(group.translation);
        item.vertical = group.block.orientation == Orientation::Vertical;
        item.background = sampleBackground(frame, item.rect);
        if (!worthCovering(frame, item.rect, item.background, group.block.score)) {
            continue;
        }
        item.foreground = readableTextColor(item.background);
        item.size = group.block.size;
        item.lineThickness = lineThickness(group.block);
        items.push_back(std::move(item));
    }
    return items;
}

}  // namespace tmw::core
