// M4-02：估計原文的文字顏色和描邊顏色。用畫好的「字」（幾條粗筆畫）測。
#include "core/text_style.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>

namespace tmw::core {
namespace {

ImageBgra canvas(int width, int height, Rgba color) {
    ImageBgra image(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint8_t* p = image.pixel(x, y);
            p[0] = color.b;
            p[1] = color.g;
            p[2] = color.r;
            p[3] = 255;
        }
    }
    return image;
}

void paint(ImageBgra& image, RectI rect, Rgba color) {
    for (int y = rect.top; y < rect.bottom; ++y) {
        for (int x = rect.left; x < rect.right; ++x) {
            std::uint8_t* p = image.pixel(x, y);
            p[0] = color.b;
            p[1] = color.g;
            p[2] = color.r;
        }
    }
}

// 「工」字：三條筆畫。outline > 0 時每條筆畫外圍先畫一圈描邊
void drawGlyph(ImageBgra& image, int left, int top, Rgba fill, int stroke = 6, int outline = 0,
               Rgba outlineColor = {}) {
    const RectI strokes[] = {{left, top, left + 40, top + stroke},
                             {left + 17, top, left + 17 + stroke, top + 40},
                             {left, top + 40 - stroke, left + 40, top + 40}};
    if (outline > 0) {
        for (const RectI& s : strokes) {
            paint(image, {s.left - outline, s.top - outline, s.right + outline, s.bottom + outline},
                  outlineColor);
        }
    }
    for (const RectI& s : strokes) {
        paint(image, s, fill);
    }
}

bool near(Rgba a, Rgba b, int tolerance = 8) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance &&
           std::abs(a.b - b.b) <= tolerance;
}

TEST(TextStyleTest, BlackTextOnWhite) {
    ImageBgra image = canvas(120, 60, {255, 255, 255, 255});
    drawGlyph(image, 10, 10, {20, 20, 20, 255});
    drawGlyph(image, 60, 10, {20, 20, 20, 255});
    // 反鋸齒：筆畫旁邊一圈灰色，不能被當成描邊
    paint(image, {9, 16, 10, 50}, {140, 140, 140, 255});
    paint(image, {50, 10, 51, 50}, {140, 140, 140, 255});
    const auto colors = estimateTextStyle(image, {0, 0, 120, 60}, {255, 255, 255, 255});
    ASSERT_TRUE(colors.has_value());
    EXPECT_TRUE(near(colors->fill, {20, 20, 20, 255}));
    EXPECT_FALSE(colors->outline.has_value()) << "反鋸齒的灰邊不是描邊";
}

TEST(TextStyleTest, ColouredTextKeepsItsColour) {
    ImageBgra image = canvas(120, 60, {20, 30, 70, 255});
    drawGlyph(image, 10, 10, {250, 210, 40, 255});
    const auto colors = estimateTextStyle(image, {0, 0, 120, 60}, {20, 30, 70, 255});
    ASSERT_TRUE(colors.has_value());
    EXPECT_TRUE(near(colors->fill, {250, 210, 40, 255})) << "遊戲裡黃色的人名";
}

TEST(TextStyleTest, FindsTheOutline) {
    ImageBgra image = canvas(120, 70, {60, 110, 200, 255});
    drawGlyph(image, 15, 15, {255, 255, 255, 255}, 6, 3, {0, 0, 0, 255});
    drawGlyph(image, 65, 15, {255, 255, 255, 255}, 6, 3, {0, 0, 0, 255});
    const auto colors = estimateTextStyle(image, {0, 0, 120, 70}, {60, 110, 200, 255});
    ASSERT_TRUE(colors.has_value());
    EXPECT_TRUE(near(colors->fill, {255, 255, 255, 255}));
    ASSERT_TRUE(colors->outline.has_value());
    EXPECT_TRUE(near(*colors->outline, {0, 0, 0, 255}));
}

TEST(TextStyleTest, TwoColoursSideBySideAreNotAnOutline) {
    // 同一段裡有黃色的標題和灰色的說明：灰色是第三種顏色，但它沒有把黃字和背景隔開
    ImageBgra image = canvas(220, 60, {17, 8, 16, 255});
    drawGlyph(image, 10, 10, {230, 200, 100, 255});
    drawGlyph(image, 60, 10, {230, 200, 100, 255});
    drawGlyph(image, 110, 10, {200, 200, 200, 255}, 4);
    drawGlyph(image, 160, 10, {200, 200, 200, 255}, 4);
    const auto colors = estimateTextStyle(image, {0, 0, 220, 60}, {17, 8, 16, 255});
    ASSERT_TRUE(colors.has_value());
    EXPECT_FALSE(colors->outline.has_value());
}

TEST(TextStyleTest, ThinStrokesStillGiveTheColour) {
    ImageBgra image = canvas(120, 60, {255, 255, 255, 255});
    drawGlyph(image, 10, 10, {200, 0, 0, 255}, 2);  // 筆畫只有 2 像素，幾乎沒有內部
    const auto colors = estimateTextStyle(image, {0, 0, 120, 60}, {255, 255, 255, 255});
    ASSERT_TRUE(colors.has_value());
    EXPECT_TRUE(near(colors->fill, {200, 0, 0, 255}));
}

TEST(TextStyleTest, NothingToEstimate) {
    const ImageBgra blank = canvas(100, 50, {255, 255, 255, 255});
    EXPECT_FALSE(estimateTextStyle(blank, {0, 0, 100, 50}, {255, 255, 255, 255}).has_value())
        << "框裡沒有字";

    ImageBgra faint = canvas(120, 60, {120, 120, 120, 255});
    drawGlyph(faint, 10, 10, {190, 190, 190, 255});
    EXPECT_FALSE(estimateTextStyle(faint, {0, 0, 120, 60}, {120, 120, 120, 255}).has_value())
        << "照著畫會看不清楚，改用黑字或白字";
}

}  // namespace
}  // namespace tmw::core
