#include "platform/screen_capture.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <chrono>
#include <optional>

#include "support/mouse_input.h"
#include "support/test_window.h"

namespace tmw::platform {
namespace {

using namespace std::chrono_literals;
using test::patternMismatches;
using test::TestWindow;

// 等待第一張畫面的時間。正式程式是 1 秒；測試放寬到 5 秒，
// 因為剛建置好的程式前幾次執行時，防毒軟體（例如 Norton）可能讓整個程序暫時變慢。
constexpr auto kTimeout = 5000ms;

core::RectI primaryMonitorRect() {
    const HMONITOR monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    return {info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom};
}

int colorMismatches(const core::ImageBgra& image, COLORREF color) {
    int mismatches = 0;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::uint8_t* p = image.pixel(x, y);
            if (p[0] != GetBValue(color) || p[1] != GetGValue(color) || p[2] != GetRValue(color)) {
                ++mismatches;
            }
        }
    }
    return mismatches;
}

class ScreenCaptureTest : public ::testing::Test {
protected:
    const core::RectI monitor_ = primaryMonitorRect();
    // 放在主螢幕左上角附近，避開螢幕中央（透鏡預設出現的位置）
    const core::RectI target_ =
        core::RectI::fromXYWH(monitor_.left + 100, monitor_.top + 100, 320, 240);
};

TEST_F(ScreenCaptureTest, CapturesWindowPixelsExactly) {
    const TestWindow window(target_);
    test::waitForComposition();

    ScreenCapture capture;
    const auto image = capture.readRegion(target_, kTimeout);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->width, 320);
    EXPECT_EQ(image->height, 240);
    EXPECT_EQ(patternMismatches(*image, 0, 0), 0);
}

// IT-05：擷取範圍的座標必須完全正確，差 1 個像素也要被抓到
TEST_F(ScreenCaptureTest, SubRegionCoordinatesAreExact) {
    const TestWindow window(target_);
    test::waitForComposition();

    ScreenCapture capture;
    const core::RectI region = core::RectI::fromXYWH(target_.left + 37, target_.top + 23, 100, 80);
    const auto image = capture.readRegion(region, kTimeout);
    ASSERT_TRUE(image.has_value());
    ASSERT_EQ(image->width, 100);
    ASSERT_EQ(image->height, 80);
    // 左上角像素的顏色就是它在測試視窗內的座標
    EXPECT_EQ(image->pixel(0, 0)[0], 37);
    EXPECT_EQ(image->pixel(0, 0)[1], 23);
    EXPECT_EQ(patternMismatches(*image, 37, 23), 0);
}

TEST_F(ScreenCaptureTest, RegionCrossingMonitorEdgeIsClipped) {
    ScreenCapture capture;
    // 中心點還在主螢幕內，右邊 40px 超出主螢幕
    const core::RectI region{monitor_.right - 60, monitor_.top + 100, monitor_.right + 40,
                             monitor_.top + 200};
    const auto image = capture.readRegion(region, kTimeout);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->width, 60);
    EXPECT_EQ(image->height, 100);
}

TEST_F(ScreenCaptureTest, RegionOffAllMonitorsFails) {
    ScreenCapture capture;
    EXPECT_FALSE(capture.readRegion({-100000, -100000, -99900, -99900}, kTimeout).has_value());
    EXPECT_FALSE(capture.readRegion({}, kTimeout).has_value());
}

// IT-01（擷取部分）：設定了 WDA_EXCLUDEFROMCAPTURE 的視窗不會出現在擷取結果中。
// 同時做正向對照：沒有排除擷取的同一個視窗必須被擷取到，證明這個測試真的測得出差異。
TEST_F(ScreenCaptureTest, ExcludedWindowDoesNotAppear) {
    const TestWindow pattern(target_);
    const core::RectI coverRect = core::inflate(target_, -40);
    {
        TestWindow::Options options;
        options.mode = TestWindow::Mode::Solid;
        options.excludeFromCapture = true;
        const TestWindow excludedCover(coverRect, options);
        test::waitForComposition();

        ScreenCapture capture;
        const auto image = capture.readRegion(target_, kTimeout);
        ASSERT_TRUE(image.has_value());
        EXPECT_EQ(patternMismatches(*image, 0, 0), 0) << "被排除的視窗出現在擷取結果中";
    }
    {
        TestWindow::Options options;
        options.mode = TestWindow::Mode::Solid;
        const TestWindow visibleCover(coverRect, options);
        test::waitForComposition();

        ScreenCapture capture;
        const auto image = capture.readRegion(target_, kTimeout);
        ASSERT_TRUE(image.has_value());
        EXPECT_GE(patternMismatches(*image, 0, 0), coverRect.width() * coverRect.height())
            << "正向對照失敗：沒有排除擷取的視窗也沒被擷取到，這個測試測不出差異";
    }
}

// 畫面連續快速變化後靜止：讀到的必須是最後的內容，不能停在中間某一張
TEST_F(ScreenCaptureTest, RapidChangesThenStaticShowsFinalContent) {
    TestWindow::Options options;
    options.mode = TestWindow::Mode::Solid;
    options.color = RGB(10, 20, 30);
    TestWindow window(target_, options);
    test::waitForComposition();

    ScreenCapture capture;
    ASSERT_TRUE(capture.readRegion(target_, kTimeout).has_value());  // 開始擷取

    const COLORREF finalColor = RGB(200, 150, 100);
    window.setSolidColor(RGB(90, 90, 90));
    test::pumpMessages(20ms);
    window.setSolidColor(RGB(40, 180, 60));
    test::pumpMessages(20ms);
    window.setSolidColor(finalColor);
    test::pumpMessages(400ms);  // 比節流間隔（100ms）長得多

    const auto image = capture.readRegion(target_, kTimeout);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(colorMismatches(*image, finalColor), 0);
}

// IT-06：系統節流。畫面每 10ms 變化一次，持續 2 秒，收到的畫面數必須受限於約每秒 10 張
TEST_F(ScreenCaptureTest, SystemThrottlesFrameRate) {
    TestWindow::Options options;
    options.mode = TestWindow::Mode::Animated;
    const TestWindow window(target_, options);
    test::waitForComposition();

    ScreenCapture capture(ScreenCapture::Options{100ms});
    ASSERT_TRUE(capture.readRegion(target_, kTimeout).has_value());
    if (!capture.stats().systemThrottling) {
        GTEST_SKIP() << "這個 Windows 版本不支援 MinUpdateInterval";
    }
    const auto before = capture.stats().framesArrived;
    test::pumpMessages(2000ms);
    const auto arrived = capture.stats().framesArrived - before;
    EXPECT_GE(arrived, 5u) << "畫面一直在變，卻幾乎沒收到新畫面";
    EXPECT_LE(arrived, 25u) << "節流沒有生效";
}

TEST_F(ScreenCaptureTest, ThumbnailIsDownscaledOnTheGpu) {
    TestWindow::Options options;
    options.mode = TestWindow::Mode::Solid;
    options.color = RGB(30, 120, 210);
    const TestWindow window(target_, options);
    test::waitForComposition();

    ScreenCapture capture;
    // 320×240：縮一半是 160×120（仍大於 128），再縮一半是 80×60
    const auto thumbnail = capture.readThumbnail(target_, 128, kTimeout);
    ASSERT_TRUE(thumbnail.has_value());
    EXPECT_EQ(thumbnail->width, 80);
    EXPECT_EQ(thumbnail->height, 60);
    // 純色縮小後仍然是同一個顏色
    EXPECT_EQ(colorMismatches(*thumbnail, options.color), 0);

    const auto small = capture.readThumbnail(target_, 64, kTimeout);
    ASSERT_TRUE(small.has_value());
    EXPECT_EQ(small->width, 40);
    EXPECT_EQ(small->height, 30);

    // 範圍本來就夠小時不縮
    const auto full = capture.readThumbnail(target_, 400, kTimeout);
    ASSERT_TRUE(full.has_value());
    EXPECT_EQ(full->width, 320);
    EXPECT_EQ(full->height, 240);
}

TEST_F(ScreenCaptureTest, ThumbnailFollowsContentChanges) {
    TestWindow::Options options;
    options.mode = TestWindow::Mode::Solid;
    options.color = RGB(10, 10, 10);
    TestWindow window(target_, options);
    test::waitForComposition();

    ScreenCapture capture;
    const auto before = capture.readThumbnail(target_, 128, kTimeout);
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(colorMismatches(*before, options.color), 0);
    const auto serialBefore = capture.stats().framesArrived;

    const COLORREF changed = RGB(240, 200, 20);
    window.setSolidColor(changed);
    test::pumpMessages(300ms);

    EXPECT_GT(capture.stats().framesArrived, serialBefore) << "畫面變了，應該收到新畫面";
    const auto after = capture.readThumbnail(target_, 128, kTimeout);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(colorMismatches(*after, changed), 0);
}

TEST_F(ScreenCaptureTest, ResetStartsNewSession) {
    const TestWindow window(target_);
    test::waitForComposition();

    ScreenCapture capture;
    ASSERT_TRUE(capture.readRegion(target_, kTimeout).has_value());
    EXPECT_EQ(capture.stats().sessionsStarted, 1u);

    capture.reset();
    const auto image = capture.readRegion(target_, kTimeout);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(capture.stats().sessionsStarted, 2u);
    EXPECT_EQ(patternMismatches(*image, 0, 0), 0);
}

// IT-07：游標停在擷取範圍內時，擷取結果必須和圖案完全相同。
// 正向對照：開啟游標擷取時，同一個位置必須看得到游標，
// 證明游標真的在範圍內、而且是顯示中的，這個測試測得出差異。
// 會移動你的滑鼠，結束後放回原位。
TEST_F(ScreenCaptureTest, CursorIsNotCaptured) {
    const TestWindow window(target_);
    test::MouseSimulator mouse;
    ASSERT_TRUE(mouse.moveTo(target_.center()));
    test::waitForComposition();

    CURSORINFO cursor{};
    cursor.cbSize = sizeof(cursor);
    ASSERT_TRUE(GetCursorInfo(&cursor));
    ASSERT_TRUE(cursor.flags & CURSOR_SHOWING) << "游標目前是隱藏的（例如沒有接滑鼠），無法測試";

    {
        ScreenCapture::Options options;
        options.captureCursor = true;
        ScreenCapture withCursor(options);
        int mismatches = 0;
        // 游標要等下一張畫面才會畫上去
        const bool cursorSeen = test::waitUntil(
            [&] {
                const auto image = withCursor.readRegion(target_, kTimeout);
                mismatches = image ? patternMismatches(*image, 0, 0) : 0;
                return mismatches > 0;
            },
            3000ms);
        ASSERT_TRUE(cursorSeen) << "正向對照失敗：開啟游標擷取也看不到游標，這個測試測不出差異";
    }

    ScreenCapture capture;
    const auto image = capture.readRegion(target_, kTimeout);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(patternMismatches(*image, 0, 0), 0) << "擷取結果中出現了游標";
}

}  // namespace
}  // namespace tmw::platform
