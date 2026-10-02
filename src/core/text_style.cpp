#include "core/text_style.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace tmw::core {
namespace {

// 和背景任一通道差這麼多以上才算墨水（反鋸齒的淡邊不算）
constexpr int kInkDistance = 60;
// 墨水至少要佔框的這個比例，否則框裡大概沒有字
constexpr double kMinInkFraction = 0.01;
// 邊緣像素離「填色—背景」這條線這麼遠，才算第三種顏色（描邊）
constexpr double kOutlineDistance = 60.0;
constexpr double kMinOutlineFraction = 0.3;
// 有描邊時，填色像素直接碰到背景的比例上限
constexpr double kMaxFillTouchingOutline = 0.15;
// 3-4 倒角距離：直走一步 3、斜走一步 4（≈ 3√2）
constexpr int kStep = 3;
constexpr int kDiagonal = 4;
// 填色和背景的亮度對比至少要這麼多才讀得清楚（WCAG 對大字的要求是 3:1）
constexpr double kMinContrast = 3.0;

struct Color {
    double r = 0;
    double g = 0;
    double b = 0;
};

Color colorAt(const ImageBgra& frame, int x, int y) {
    const std::uint8_t* p = frame.pixel(x, y);
    return Color{static_cast<double>(p[2]), static_cast<double>(p[1]), static_cast<double>(p[0])};
}

Color toColor(Rgba c) {
    return Color{static_cast<double>(c.r), static_cast<double>(c.g), static_cast<double>(c.b)};
}

double distance(Color a, Color b) {
    return std::sqrt((a.r - b.r) * (a.r - b.r) + (a.g - b.g) * (a.g - b.g) +
                     (a.b - b.b) * (a.b - b.b));
}

// 點到線段的距離（反鋸齒的像素落在填色和背景的連線上）
double distanceToSegment(Color p, Color a, Color b) {
    const Color ab{b.r - a.r, b.g - a.g, b.b - a.b};
    const double length = ab.r * ab.r + ab.g * ab.g + ab.b * ab.b;
    double t =
        length == 0 ? 0 : ((p.r - a.r) * ab.r + (p.g - a.g) * ab.g + (p.b - a.b) * ab.b) / length;
    t = std::clamp(t, 0.0, 1.0);
    return distance(p, Color{a.r + ab.r * t, a.g + ab.g * t, a.b + ab.b * t});
}

// WCAG 的相對亮度
double luminance(Color c) {
    const auto linear = [](double v) {
        v /= 255.0;
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b);
}

double contrast(Color a, Color b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

std::uint8_t medianOf(std::vector<double>& values) {
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return static_cast<std::uint8_t>(std::lround(*middle));
}

Rgba median(const std::vector<Color>& colors) {
    std::vector<double> r;
    std::vector<double> g;
    std::vector<double> b;
    for (const Color& c : colors) {
        r.push_back(c.r);
        g.push_back(c.g);
        b.push_back(c.b);
    }
    return Rgba{medianOf(r), medianOf(g), medianOf(b), 255};
}

}  // namespace

double contrastRatio(Rgba a, Rgba b) {
    return contrast(toColor(a), toColor(b));
}

std::optional<TextStyle> estimateTextStyle(const ImageBgra& frame, const RectI& rect,
                                           Rgba background) {
    const RectI area{std::max(0, rect.left), std::max(0, rect.top),
                     std::min(frame.width, rect.right), std::min(frame.height, rect.bottom)};
    if (area.empty()) {
        return std::nullopt;
    }
    const int width = area.width();
    const int height = area.height();
    const auto isInk = [&](int x, int y) {
        const std::uint8_t* p = frame.pixel(x, y);
        return std::abs(p[2] - background.r) > kInkDistance ||
               std::abs(p[1] - background.g) > kInkDistance ||
               std::abs(p[0] - background.b) > kInkDistance;
    };
    std::vector<bool> ink(static_cast<std::size_t>(width) * height);
    std::size_t inkCount = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool yes = isInk(area.left + x, area.top + y);
            ink[static_cast<std::size_t>(y) * width + x] = yes;
            inkCount += yes ? 1 : 0;
        }
    }
    if (inkCount < 10 || static_cast<double>(inkCount) < kMinInkFraction * width * height) {
        return std::nullopt;
    }
    const auto index = [width](int x, int y) { return static_cast<std::size_t>(y) * width + x; };
    // 框外當成墨水：字貼著框邊時，貼邊的筆畫不會被誤當成邊緣
    const auto inkAt = [&](int x, int y) {
        return x < 0 || y < 0 || x >= width || y >= height || ink[index(x, y)];
    };
    const auto touchesBackground = [&](int x, int y) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (!inkAt(x + dx, y + dy)) {
                    return true;
                }
            }
        }
        return false;
    };

    // 每個墨水像素離背景有多深（3-4 倒角距離轉換，兩趟掃描：直走一步算 3、斜走算 4，
    // 比只數步數更接近真正的距離，粗細才量得出差別）。最深的是筆畫正中央：
    // 反鋸齒的淡邊、描邊都在外圍，所以中央的顏色就是填色
    const int far = kStep * (width + height);
    std::vector<int> depth(ink.size(), 0);
    const auto depthAt = [&](int x, int y) {
        return x < 0 || y < 0 || x >= width || y >= height ? far : depth[index(x, y)];
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (ink[index(x, y)]) {
                depth[index(x, y)] = std::min({depthAt(x - 1, y) + kStep, depthAt(x, y - 1) + kStep,
                                               depthAt(x - 1, y - 1) + kDiagonal,
                                               depthAt(x + 1, y - 1) + kDiagonal});
            }
        }
    }
    for (int y = height - 1; y >= 0; --y) {
        for (int x = width - 1; x >= 0; --x) {
            if (ink[index(x, y)]) {
                depth[index(x, y)] = std::min(
                    {depth[index(x, y)], depthAt(x + 1, y) + kStep, depthAt(x, y + 1) + kStep,
                     depthAt(x + 1, y + 1) + kDiagonal, depthAt(x - 1, y + 1) + kDiagonal});
            }
        }
    }

    std::vector<std::pair<int, Color>> all;  // （深度, 顏色）
    std::vector<Color> edge;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!ink[index(x, y)]) {
                continue;
            }
            const Color c = colorAt(frame, area.left + x, area.top + y);
            all.emplace_back(depth[index(x, y)], c);
            if (!inkAt(x - 1, y) || !inkAt(x + 1, y) || !inkAt(x, y - 1) || !inkAt(x, y + 1)) {
                edge.push_back(c);
            }
        }
    }

    // 填色：最深的兩成
    std::sort(all.begin(), all.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    const Color back = toColor(background);
    std::vector<std::pair<double, Color>> deepest;  // （離背景多遠, 顏色）
    for (std::size_t i = 0; i < std::max<std::size_t>(5, all.size() / 5) && i < all.size(); ++i) {
        deepest.emplace_back(distance(all[i].second, back), all[i].second);
    }
    // 筆畫只有兩三個像素粗時（漫畫），連中央都混到了反鋸齒：再取其中離背景最遠的一半
    std::sort(deepest.begin(), deepest.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<Color> core;
    for (std::size_t i = 0; i < std::max<std::size_t>(1, deepest.size() / 2); ++i) {
        core.push_back(deepest[i].second);
    }
    TextStyle out;
    out.fill = median(core);
    const Color fill = toColor(out.fill);

    // 描邊：邊緣有夠多「不是填色和背景之間的漸層」的顏色，而且填色幾乎不直接碰到背景
    // （描邊把兩者隔開）。後者排除「黃色標題和灰色說明在同一段」：灰字也是第三種顏色，
    // 但黃字直接貼著背景。
    std::vector<Color> third;
    for (const Color& c : edge) {
        if (distanceToSegment(c, fill, back) > kOutlineDistance) {
            third.push_back(c);
        }
    }
    std::size_t fillLike = 0;
    std::size_t fillTouching = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (ink[index(x, y)] &&
                distance(colorAt(frame, area.left + x, area.top + y), fill) < kOutlineDistance) {
                ++fillLike;
                fillTouching += touchesBackground(x, y) ? 1 : 0;
            }
        }
    }
    if (!edge.empty() &&
        static_cast<double>(third.size()) >=
            kMinOutlineFraction * static_cast<double>(edge.size()) &&
        static_cast<double>(fillTouching) <=
            kMaxFillTouchingOutline * static_cast<double>(fillLike)) {
        out.outline = median(third);
    }
    if (contrast(fill, back) < kMinContrast && !out.outline) {
        return std::nullopt;  // 照著畫會看不清楚
    }
    return out;
}

}  // namespace tmw::core
