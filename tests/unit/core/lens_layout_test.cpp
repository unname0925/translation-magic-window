#include "core/lens_layout.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace tmw::core {
namespace {

constexpr Rgba kAccent{59, 130, 246, 230};
constexpr unsigned kAllDpis[] = {96, 120, 144, 168, 192};

TEST(ColorTest, PremultipliedArgb) {
    EXPECT_EQ(toPremultipliedArgb({255, 0, 0, 255}), 0xFFFF0000u);
    EXPECT_EQ(toPremultipliedArgb({255, 255, 255, 128}), 0x80808080u);
    EXPECT_EQ(toPremultipliedArgb({255, 255, 255, 0}), 0x00000000u);
    EXPECT_EQ(alphaOf(toPremultipliedArgb({1, 2, 3, 77})), 77);
}

TEST(LensMetricsTest, UnscaledAt96Dpi) {
    const LensMetrics m = scaledLensMetrics(96);
    EXPECT_EQ(m.outerGrab, 6);
    EXPECT_EQ(m.border, 3);
    EXPECT_EQ(m.innerGrab, 6);
    EXPECT_EQ(m.handleWidth, 72);
    EXPECT_EQ(m.handleHeight, 22);
}

TEST(LensMetricsTest, ScaledAt150Percent) {
    const LensMetrics m = scaledLensMetrics(144);
    EXPECT_EQ(m.outerGrab, 9);
    EXPECT_EQ(m.border, 5);  // 4.5 四捨五入
    EXPECT_EQ(m.innerGrab, 9);
    EXPECT_EQ(m.handleWidth, 108);
    EXPECT_EQ(m.handleHeight, 33);
}

TEST(LensLayoutTest, KnownLayoutAt96Dpi) {
    const LensLayout layout = computeLensLayout(lensWindowSizeForContent({480, 270}, 96), 96);
    EXPECT_EQ(layout.window, (SizeI{498, 304}));
    EXPECT_EQ(layout.handle, (RectI{6, 0, 78, 22}));
    EXPECT_EQ(layout.frame, (RectI{0, 16, 498, 304}));
    EXPECT_EQ(layout.border, (RectI{6, 22, 492, 298}));
    EXPECT_EQ(layout.content, (RectI{9, 25, 489, 295}));
    EXPECT_EQ(layout.clickThrough, (RectI{15, 31, 483, 289}));
}

TEST(LensLayoutTest, WindowSizeForContentRoundTripsAtAllDpis) {
    for (unsigned dpi : kAllDpis) {
        for (SizeI content : {SizeI{480, 270}, SizeI{120, 60}, SizeI{1001, 333}}) {
            const LensLayout layout =
                computeLensLayout(lensWindowSizeForContent(content, dpi), dpi);
            EXPECT_EQ(layout.content.width(), content.width) << "dpi=" << dpi;
            EXPECT_EQ(layout.content.height(), content.height) << "dpi=" << dpi;
        }
    }
}

TEST(LensLayoutTest, HandleSitsOnTopOfVisibleBorder) {
    for (unsigned dpi : kAllDpis) {
        const LensLayout layout = computeLensLayout(minimumLensWindowSize(dpi), dpi);
        EXPECT_EQ(layout.handle.top, 0);
        EXPECT_EQ(layout.handle.bottom, layout.border.top) << "dpi=" << dpi;
        EXPECT_EQ(layout.handle.left, layout.border.left) << "dpi=" << dpi;
    }
}

TEST(LensLayoutTest, MinimumSizeKeepsMinimumContent) {
    for (unsigned dpi : kAllDpis) {
        const LensMetrics m = scaledLensMetrics(dpi);
        const LensLayout layout = computeLensLayout(minimumLensWindowSize(dpi), dpi);
        EXPECT_EQ(layout.content.width(), m.minContentWidth) << "dpi=" << dpi;
        EXPECT_EQ(layout.content.height(), m.minContentHeight) << "dpi=" << dpi;
        EXPECT_FALSE(layout.clickThrough.empty()) << "dpi=" << dpi;
    }
}

class LensHitTestTest : public ::testing::Test {
protected:
    const LensLayout layout = computeLensLayout(lensWindowSizeForContent({480, 270}, 96), 96);
};

TEST_F(LensHitTestTest, HandleMovesTheLens) {
    EXPECT_EQ(hitTestLens(layout, layout.handle.center()), LensHitZone::Move);
    EXPECT_EQ(hitTestLens(layout, {layout.handle.left, 0}), LensHitZone::Move);
}

TEST_F(LensHitTestTest, CenterIsClickThrough) {
    EXPECT_EQ(hitTestLens(layout, layout.content.center()), LensHitZone::Transparent);
    EXPECT_EQ(hitTestLens(layout, {layout.clickThrough.left, 150}), LensHitZone::Transparent);
}

TEST_F(LensHitTestTest, AreaBesideHandleIsClickThrough) {
    EXPECT_EQ(hitTestLens(layout, {layout.handle.right + 10, 5}), LensHitZone::Transparent);
    EXPECT_EQ(hitTestLens(layout, {2, 5}), LensHitZone::Transparent);
}

TEST_F(LensHitTestTest, OutsideWindowIsClickThrough) {
    EXPECT_EQ(hitTestLens(layout, {-1, 150}), LensHitZone::Transparent);
    EXPECT_EQ(hitTestLens(layout, {layout.window.width, 150}), LensHitZone::Transparent);
    EXPECT_EQ(hitTestLens(layout, {200, layout.window.height}), LensHitZone::Transparent);
}

TEST_F(LensHitTestTest, EdgesResize) {
    const int midY = layout.content.center().y;
    const int midX = layout.content.center().x;
    EXPECT_EQ(hitTestLens(layout, {layout.border.left, midY}), LensHitZone::Left);
    EXPECT_EQ(hitTestLens(layout, {layout.border.right - 1, midY}), LensHitZone::Right);
    EXPECT_EQ(hitTestLens(layout, {midX, layout.border.top}), LensHitZone::Top);
    EXPECT_EQ(hitTestLens(layout, {midX, layout.border.bottom - 1}), LensHitZone::Bottom);
}

TEST_F(LensHitTestTest, InvisibleGrabZonesAlsoResize) {
    const int midY = layout.content.center().y;
    EXPECT_EQ(hitTestLens(layout, {layout.frame.left, midY}), LensHitZone::Left);  // 外側
    EXPECT_EQ(hitTestLens(layout, {layout.clickThrough.left - 1, midY}),
              LensHitZone::Left);  // 內側
}

TEST_F(LensHitTestTest, CornersResizeDiagonally) {
    const RectI& f = layout.frame;
    EXPECT_EQ(hitTestLens(layout, {f.left, f.bottom - 1}), LensHitZone::BottomLeft);
    EXPECT_EQ(hitTestLens(layout, {f.right - 1, f.bottom - 1}), LensHitZone::BottomRight);
    EXPECT_EQ(hitTestLens(layout, {f.right - 1, f.top}), LensHitZone::TopRight);
    EXPECT_EQ(hitTestLens(layout, {f.left, f.top}), LensHitZone::TopLeft);
    // 角落區沿著邊延伸
    EXPECT_EQ(hitTestLens(layout, {f.right - 1, f.bottom - 15}), LensHitZone::BottomRight);
    EXPECT_EQ(hitTestLens(layout, {f.right - 1, f.bottom - 17}), LensHitZone::Right);
}

std::vector<std::uint32_t> render(const LensLayout& layout) {
    std::vector<std::uint32_t> pixels(static_cast<size_t>(layout.window.width) *
                                      layout.window.height);
    renderLens(layout, kAccent, pixels);
    return pixels;
}

std::uint32_t pixelAt(const std::vector<std::uint32_t>& pixels, const LensLayout& layout,
                      PointI p) {
    return pixels[static_cast<size_t>(p.y) * layout.window.width + p.x];
}

// 最重要的不變條件：看得到的像素（alpha > 0）和點得到的位置必須完全一致。
// 如果不一致，中間的穿透區會出現「看起來透明卻點不到底下」或反過來的縫隙。
TEST(LensRenderTest, VisiblePixelsMatchHitTestEverywhere) {
    for (unsigned dpi : kAllDpis) {
        for (SizeI size : {lensWindowSizeForContent({480, 270}, dpi), minimumLensWindowSize(dpi),
                           SizeI{10, 10}, SizeI{1, 1}}) {
            const LensLayout layout = computeLensLayout(size, dpi);
            const auto pixels = render(layout);
            int mismatches = 0;
            for (int y = 0; y < size.height; ++y) {
                for (int x = 0; x < size.width; ++x) {
                    const bool visible = alphaOf(pixelAt(pixels, layout, {x, y})) != 0;
                    const bool clickable = hitTestLens(layout, {x, y}) != LensHitZone::Transparent;
                    if (visible != clickable) {
                        ++mismatches;
                    }
                }
            }
            EXPECT_EQ(mismatches, 0)
                << "dpi=" << dpi << " size=" << size.width << "x" << size.height;
        }
    }
}

TEST(LensRenderTest, PaintsBorderHandleAndGrabZones) {
    const LensLayout layout = computeLensLayout(lensWindowSizeForContent({480, 270}, 96), 96);
    const auto pixels = render(layout);
    const std::uint32_t accent = toPremultipliedArgb(kAccent);
    const int midY = layout.content.center().y;

    EXPECT_EQ(pixelAt(pixels, layout, layout.content.center()), 0u);              // 中間透明
    EXPECT_EQ(pixelAt(pixels, layout, {layout.border.left, midY}), accent);       // 可見邊框
    EXPECT_EQ(alphaOf(pixelAt(pixels, layout, {layout.frame.left, midY})), 1);    // 外側抓取區
    EXPECT_EQ(alphaOf(pixelAt(pixels, layout, {layout.content.left, midY})), 1);  // 內側抓取區
    EXPECT_EQ(pixelAt(pixels, layout, {layout.handle.left, 0}), accent);          // 把手
}

TEST(LensRenderTest, HandleHasGripDots) {
    const LensLayout layout = computeLensLayout(lensWindowSizeForContent({480, 270}, 96), 96);
    const auto pixels = render(layout);
    const std::uint32_t accent = toPremultipliedArgb(kAccent);
    int dotPixels = 0;
    for (int y = layout.handle.top; y < layout.handle.bottom; ++y) {
        for (int x = layout.handle.left; x < layout.handle.right; ++x) {
            const std::uint32_t pixel = pixelAt(pixels, layout, {x, y});
            if (pixel != accent) {
                ++dotPixels;
            }
        }
    }
    EXPECT_EQ(dotPixels, 6 * 2 * 2);  // 6 個點，每個 2×2 像素
}

TEST(LensRenderTest, RejectsWrongBufferSize) {
    const LensLayout layout = computeLensLayout({100, 100}, 96);
    std::vector<std::uint32_t> tooSmall(99 * 100);
    EXPECT_THROW(renderLens(layout, kAccent, tooSmall), std::invalid_argument);
}

}  // namespace
}  // namespace tmw::core
