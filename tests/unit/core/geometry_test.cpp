#include "core/geometry.h"

#include <gtest/gtest.h>

namespace tmw::core {
namespace {

TEST(RectITest, SizeAndEmpty) {
    const RectI rect = RectI::fromXYWH(10, 20, 300, 200);
    EXPECT_EQ(rect, (RectI{10, 20, 310, 220}));
    EXPECT_EQ(rect.width(), 300);
    EXPECT_EQ(rect.height(), 200);
    EXPECT_FALSE(rect.empty());

    EXPECT_TRUE(RectI{}.empty());
    EXPECT_TRUE((RectI{10, 10, 10, 50}).empty());  // 寬度為 0
    EXPECT_TRUE((RectI{10, 10, 5, 50}).empty());   // 寬度為負
}

TEST(RectITest, ContainsExcludesRightAndBottomEdges) {
    const RectI rect{0, 0, 100, 50};
    EXPECT_TRUE(rect.contains({0, 0}));
    EXPECT_TRUE(rect.contains({99, 49}));
    EXPECT_FALSE(rect.contains({100, 0}));
    EXPECT_FALSE(rect.contains({0, 50}));
    EXPECT_FALSE(rect.contains({-1, 10}));
}

TEST(RectITest, Center) {
    EXPECT_EQ((RectI{0, 0, 100, 50}).center(), (PointI{50, 25}));
    EXPECT_EQ((RectI{-200, -100, 0, 0}).center(), (PointI{-100, -50}));
}

TEST(GeometryTest, IntersectOverlapping) {
    EXPECT_EQ(intersect({0, 0, 100, 100}, {50, 60, 200, 200}), (RectI{50, 60, 100, 100}));
}

TEST(GeometryTest, IntersectContained) {
    EXPECT_EQ(intersect({0, 0, 100, 100}, {10, 10, 20, 20}), (RectI{10, 10, 20, 20}));
}

TEST(GeometryTest, IntersectDisjointIsEmpty) {
    EXPECT_EQ(intersect({0, 0, 100, 100}, {200, 200, 300, 300}), RectI{});
}

TEST(GeometryTest, IntersectTouchingEdgesIsEmpty) {
    EXPECT_EQ(intersect({0, 0, 100, 100}, {100, 0, 200, 100}), RectI{});
}

TEST(GeometryTest, Offset) {
    EXPECT_EQ(offset({10, 20, 30, 40}, 5, -10), (RectI{15, 10, 35, 30}));
}

TEST(GeometryTest, InflateGrowsAndShrinks) {
    EXPECT_EQ(inflate({10, 10, 20, 20}, 2), (RectI{8, 8, 22, 22}));
    EXPECT_EQ(inflate({10, 10, 20, 20}, -2), (RectI{12, 12, 18, 18}));
}

TEST(GeometryTest, InflateShrinkingPastZeroIsEmpty) {
    EXPECT_EQ(inflate({10, 10, 20, 20}, -5), RectI{});
    EXPECT_EQ(inflate({10, 10, 20, 20}, -6), RectI{});
}

// 以下對應 execution-plan.md 的 UT-10：透鏡範圍換算成螢幕擷取畫面內的座標
const RectI kPrimaryMonitor{0, 0, 1920, 1080};
const RectI kLeftMonitor{-1920, 0, 0, 1080};  // 放在主螢幕左邊的副螢幕

TEST(ScreenToMonitorLocalTest, FullyInsidePrimaryMonitor) {
    EXPECT_EQ(screenToMonitorLocal({100, 200, 500, 400}, kPrimaryMonitor),
              (RectI{100, 200, 500, 400}));
}

TEST(ScreenToMonitorLocalTest, PartlyOffScreenIsClipped) {
    EXPECT_EQ(screenToMonitorLocal({1800, 1000, 2000, 1200}, kPrimaryMonitor),
              (RectI{1800, 1000, 1920, 1080}));
    EXPECT_EQ(screenToMonitorLocal({-50, -30, 100, 100}, kPrimaryMonitor), (RectI{0, 0, 100, 100}));
}

TEST(ScreenToMonitorLocalTest, NegativeCoordinatesOnLeftMonitor) {
    EXPECT_EQ(screenToMonitorLocal({-500, 100, -100, 300}, kLeftMonitor),
              (RectI{1420, 100, 1820, 300}));
}

TEST(ScreenToMonitorLocalTest, SpanningTwoMonitorsKeepsOnlyRequestedMonitor) {
    const RectI lens{-100, 100, 100, 300};
    EXPECT_EQ(screenToMonitorLocal(lens, kPrimaryMonitor), (RectI{0, 100, 100, 300}));
    EXPECT_EQ(screenToMonitorLocal(lens, kLeftMonitor), (RectI{1820, 100, 1920, 300}));
}

TEST(ScreenToMonitorLocalTest, OffMonitorIsEmpty) {
    EXPECT_EQ(screenToMonitorLocal({2000, 0, 2100, 100}, kPrimaryMonitor), RectI{});
}

TEST(ScaleForDpiTest, CommonScalingFactors) {
    EXPECT_EQ(scaleForDpi(8, 96), 8);    // 100%
    EXPECT_EQ(scaleForDpi(8, 120), 10);  // 125%
    EXPECT_EQ(scaleForDpi(8, 144), 12);  // 150%
    EXPECT_EQ(scaleForDpi(8, 168), 14);  // 175%
    EXPECT_EQ(scaleForDpi(8, 192), 16);  // 200%
}

TEST(ScaleForDpiTest, RoundsToNearest) {
    EXPECT_EQ(scaleForDpi(1, 144), 2);  // 1.5 → 2
    EXPECT_EQ(scaleForDpi(1, 120), 1);  // 1.25 → 1
    EXPECT_EQ(scaleForDpi(0, 144), 0);
}

}  // namespace
}  // namespace tmw::core
