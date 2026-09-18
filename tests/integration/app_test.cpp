// 主程式的整合測試：在另一個程序中啟動真正的 TranslationMagicWindow.exe，
// 從外部用真實的滑鼠輸入和螢幕擷取檢查它的行為（見 docs/execution-plan.md 5.4）。
//
// 每個測試都用 --data-dir 指定一個暫存資料夾，不會動到你的擷取資料夾。
// 執行前請先結束正在執行的主程式（單一執行個體）。

#include <windows.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "app/app_identity.h"
#include "core/geometry.h"
#include "core/lens_layout.h"
#include "platform/lens_window.h"
#include "platform/png_file.h"
#include "platform/screen_capture.h"
#include "support/app_process.h"
#include "support/mouse_input.h"
#include "support/test_window.h"

namespace tmw {
namespace {

using namespace std::chrono_literals;
using test::AppProcess;
using test::MouseSimulator;
using test::patternMismatches;
using test::TestWindow;
using test::waitUntil;

// 防毒軟體（例如 Norton）第一次執行新建置的執行檔時，會先掃描 20 秒以上
constexpr auto kLaunchTimeout = 30000ms;
constexpr auto kExitTimeout = 10000ms;
constexpr auto kCaptureTimeout = 5000ms;
constexpr auto kUiTimeout = 3000ms;

// 測試視窗比透鏡四周各大這麼多，透鏡拖動後還是蓋在測試視窗上
constexpr int kPatternMargin = 100;

std::string describe(const core::RectI& rect) {
    std::ostringstream text;
    text << "{" << rect.left << ", " << rect.top << ", " << rect.right << ", " << rect.bottom
         << "}";
    return text.str();
}

core::RectI windowRectOf(HWND hwnd) {
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    return {rect.left, rect.top, rect.right, rect.bottom};
}

HWND windowAt(core::PointI point) {
    return WindowFromPoint({point.x, point.y});
}

// 透鏡目前的位置和版面。版面用 core 的 computeLensLayout 算出，和主程式用的是同一套公式。
struct LensGeometry {
    core::RectI window;  // 螢幕座標
    core::LensLayout layout;

    core::RectI toScreen(const core::RectI& rect) const {
        return core::offset(rect, window.left, window.top);
    }
    core::PointI toScreen(core::PointI point) const {
        return {point.x + window.left, point.y + window.top};
    }
};

std::filesystem::path makeTemporaryDataDirectory() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        (L"tmw-integration-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(path);
    return path;
}

class AppTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_FALSE(AppProcess::isAnyInstanceRunning())
            << "主程式正在執行。整合測試要啟動自己的執行個體，請先從系統匣結束它";
        dataDirectory_ = makeTemporaryDataDirectory();
        app_ = std::make_unique<AppProcess>(
            std::vector<std::wstring>{L"--data-dir", dataDirectory_.wstring()});
        lens_ = app_->waitForWindow(platform::LensWindow::kClassName, kLaunchTimeout);
        ASSERT_NE(lens_, nullptr) << "主程式沒有顯示透鏡"
                                  << (app_->hasExited() ? "（程式已經結束）" : "");
        // 透鏡視窗建立後才會移到定位並顯示出來
        ASSERT_TRUE(waitUntil([&] { return IsWindowVisible(lens_) != FALSE; }, kLaunchTimeout))
            << "透鏡一直沒有顯示";
        ASSERT_TRUE(app_->isResponsive(kLaunchTimeout)) << "主程式沒有回應";
    }

    void TearDown() override {
        if (app_) {
            if (!app_->hasExited()) {
                EXPECT_EQ(app_->requestExit(kExitTimeout), std::optional<DWORD>{0})
                    << "主程式沒有正常結束";
            }
            app_.reset();
        }
        std::error_code ignored;
        std::filesystem::remove_all(dataDirectory_, ignored);
    }

    LensGeometry lensGeometry() const {
        LensGeometry geometry;
        geometry.window = windowRectOf(lens_);
        const UINT dpi = GetDpiForWindow(lens_);
        geometry.layout = core::computeLensLayout(
            {geometry.window.width(), geometry.window.height()}, dpi == 0 ? 96 : dpi);
        return geometry;
    }

    // 在透鏡正下方放一個 Pattern 測試視窗，四周比透鏡各大 kPatternMargin。
    std::unique_ptr<TestWindow> placePatternUnderLens() const {
        auto pattern =
            std::make_unique<TestWindow>(core::inflate(windowRectOf(lens_), kPatternMargin));
        // 測試視窗和透鏡都是最上層視窗，後建立的會蓋在上面；把測試視窗移到透鏡的正下方
        SetWindowPos(pattern->hwnd(), lens_, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        test::waitForComposition();
        return pattern;
    }

    DWORD lensThreadId() const { return GetWindowThreadProcessId(lens_, nullptr); }

    // 點擊前的安全檢查：只點測試視窗或透鏡，不會點到你的其他程式
    bool isSafeToClick(core::PointI point, const TestWindow& pattern) const {
        const HWND target = windowAt(point);
        return target == lens_ || target == pattern.hwnd();
    }

    std::filesystem::path dataDirectory_;
    std::unique_ptr<AppProcess> app_;
    HWND lens_ = nullptr;
};

// IT-01：把真正的透鏡放在測試圖案上，擷取整個透鏡範圍（含邊框和把手），
// 結果必須和圖案完全相同，差 1 個色階都不行（內側抓取區的 alpha 只有 1/255）。
// 正向對照：先確認透鏡真的蓋在圖案上面；擷取方法測得出「有視窗蓋住」則由
// ScreenCaptureTest.ExcludedWindowDoesNotAppear 證明。
TEST_F(AppTest, LensIsExcludedFromCapture) {
    const auto pattern = placePatternUnderLens();
    const LensGeometry lens = lensGeometry();
    ASSERT_EQ(windowAt(lens.toScreen(lens.layout.handle.center())), lens_)
        << "透鏡沒有蓋在測試圖案上，這個測試測不出差異";
    const core::PointI leftBorder{lens.layout.border.left + 1, lens.layout.border.center().y};
    ASSERT_EQ(windowAt(lens.toScreen(leftBorder)), lens_);

    platform::ScreenCapture capture;
    const auto image = capture.readRegion(lens.window, kCaptureTimeout);
    ASSERT_TRUE(image.has_value());
    ASSERT_EQ(image->width, lens.window.width());
    ASSERT_EQ(image->height, lens.window.height());
    EXPECT_EQ(patternMismatches(*image, lens.window.left - pattern->rect().left,
                                lens.window.top - pattern->rect().top),
              0)
        << "透鏡出現在擷取結果中";
}

// IT-02：點擊透鏡中間，底下的視窗必須收到點擊，座標也要正確。
// 除了中心，也點穿透範圍的左上角和右下角最邊緣的像素，確認穿透範圍沒有差 1 個像素。
TEST_F(AppTest, ClicksInsideLensReachWindowBelow) {
    const auto pattern = placePatternUnderLens();
    const LensGeometry lens = lensGeometry();
    const core::RectI& inner = lens.layout.clickThrough;
    const std::vector<core::PointI> points = {
        inner.center(), {inner.left, inner.top}, {inner.right - 1, inner.bottom - 1}};

    MouseSimulator mouse;
    for (const core::PointI local : points) {
        ASSERT_EQ(core::hitTestLens(lens.layout, local), core::LensHitZone::Transparent);
        const core::PointI point = lens.toScreen(local);
        SCOPED_TRACE("點擊 (" + std::to_string(point.x) + ", " + std::to_string(point.y) + ")");
        ASSERT_TRUE(isSafeToClick(point, *pattern)) << "這個位置不是測試視窗或透鏡，不點擊";

        pattern->clearMouseEvents();
        ASSERT_TRUE(mouse.click(point));
        ASSERT_TRUE(waitUntil([&] { return pattern->mouseEvents().size() >= 2; }, kUiTimeout))
            << "底下的測試視窗沒有收到點擊，透鏡中間沒有穿透";

        const core::PointI expected{point.x - pattern->rect().left, point.y - pattern->rect().top};
        const auto& events = pattern->mouseEvents();
        EXPECT_EQ(events[0].message, static_cast<UINT>(WM_LBUTTONDOWN));
        EXPECT_EQ(events[1].message, static_cast<UINT>(WM_LBUTTONUP));
        EXPECT_EQ(events[0].clientPoint, expected);
    }
}

// IT-03：拖動把手，透鏡要跟著移動完全相同的距離，底下的視窗不能收到點擊。
// 正向對照：拖動後點擊透鏡中間，同一個測試視窗必須收得到點擊。
TEST_F(AppTest, DraggingHandleMovesLensWithoutClickingBelow) {
    const auto pattern = placePatternUnderLens();
    const LensGeometry before = lensGeometry();
    const core::PointI from = before.toScreen(before.layout.handle.center());
    ASSERT_EQ(windowAt(from), lens_) << "把手的位置不是透鏡，不拖動";

    constexpr int kDx = 48;
    constexpr int kDy = 32;
    MouseSimulator mouse;
    ASSERT_TRUE(mouse.drag(from, {from.x + kDx, from.y + kDy}, lensThreadId()))
        << "透鏡沒有進入拖動狀態";

    const core::RectI expected = core::offset(before.window, kDx, kDy);
    EXPECT_TRUE(waitUntil([&] { return windowRectOf(lens_) == expected; }, kUiTimeout))
        << "預期 " << describe(expected) << "，實際 " << describe(windowRectOf(lens_));
    test::pumpMessages(100ms);
    EXPECT_TRUE(pattern->mouseEvents().empty()) << "拖動透鏡時，底下的視窗收到了點擊";

    const LensGeometry after = lensGeometry();
    const core::PointI center = after.toScreen(after.layout.clickThrough.center());
    ASSERT_TRUE(isSafeToClick(center, *pattern));
    ASSERT_TRUE(mouse.click(center));
    EXPECT_TRUE(waitUntil([&] { return pattern->mouseEvents().size() >= 2; }, kUiTimeout))
        << "正向對照失敗：測試視窗收不到點擊，上面的「沒有收到點擊」不代表什麼";
}

// IT-03：拖動右下角，透鏡的大小要跟著改變完全相同的量，左上角不動。
TEST_F(AppTest, DraggingCornerResizesLens) {
    const auto pattern = placePatternUnderLens();
    const LensGeometry before = lensGeometry();
    const core::PointI local{before.layout.frame.right - 3, before.layout.frame.bottom - 3};
    ASSERT_EQ(core::hitTestLens(before.layout, local), core::LensHitZone::BottomRight);
    const core::PointI from = before.toScreen(local);
    ASSERT_EQ(windowAt(from), lens_) << "右下角的位置不是透鏡，不拖動";

    constexpr int kDx = 40;
    constexpr int kDy = 30;
    MouseSimulator mouse;
    ASSERT_TRUE(mouse.drag(from, {from.x + kDx, from.y + kDy}, lensThreadId()))
        << "透鏡沒有進入縮放狀態";

    const core::RectI expected{before.window.left, before.window.top, before.window.right + kDx,
                               before.window.bottom + kDy};
    EXPECT_TRUE(waitUntil([&] { return windowRectOf(lens_) == expected; }, kUiTimeout))
        << "預期 " << describe(expected) << "，實際 " << describe(windowRectOf(lens_));
    test::pumpMessages(100ms);
    EXPECT_TRUE(pattern->mouseEvents().empty()) << "縮放透鏡時，底下的視窗收到了點擊";
}

// M0 的端到端測試：透鏡底下的畫面穩定後，主程式自動存出的 PNG 必須剛好是擷取範圍
// （可見邊框的內緣）的內容，和測試圖案逐像素相同。
// 同時驗證了：--data-dir、畫面變化後自動觸發、擷取範圍的座標、透鏡和游標都不在擷取結果中。
TEST_F(AppTest, AutoSavedCaptureMatchesContentUnderLens) {
    const auto pattern = placePatternUnderLens();
    const LensGeometry lens = lensGeometry();
    const core::RectI content = lens.toScreen(lens.layout.content);
    const std::filesystem::path captures = dataDirectory_ / L"captures";

    std::size_t files = 0;
    int lastMismatches = -1;
    const bool found = waitUntil(
        [&] {
            std::error_code error;
            if (!std::filesystem::exists(captures, error)) {
                return false;
            }
            files = 0;
            for (const auto& entry : std::filesystem::directory_iterator(captures, error)) {
                if (entry.path().extension() != L".png") {
                    continue;
                }
                ++files;
                try {
                    const core::ImageBgra image = platform::loadImage(entry.path());
                    if (image.width != content.width() || image.height != content.height()) {
                        continue;
                    }
                    lastMismatches = patternMismatches(image, content.left - pattern->rect().left,
                                                       content.top - pattern->rect().top);
                    if (lastMismatches == 0) {
                        return true;
                    }
                } catch (const std::exception&) {
                    // 檔案可能還在寫入，下一輪再讀
                }
            }
            return false;
        },
        15000ms);
    EXPECT_TRUE(found) << "15 秒內沒有存出和測試圖案相同的 PNG（共 " << files
                       << " 個 PNG，最後比對的一張有 " << lastMismatches << " 個像素不同）";
}

// IT-04：已經有主程式在執行時，第二個執行個體必須自行結束，
// 並通知第一個執行個體把透鏡顯示出來（先把透鏡隱藏，才看得出有沒有通知）。
TEST_F(AppTest, SecondInstanceExitsAndShowsTheLens) {
    ASSERT_TRUE(app_->postCommand(app::kCommandToggleLens));
    ASSERT_TRUE(waitUntil([&] { return IsWindowVisible(lens_) == FALSE; }, kUiTimeout))
        << "透鏡沒有隱藏";

    AppProcess second({L"--data-dir", dataDirectory_.wstring()});
    const std::optional<DWORD> exitCode = second.waitForExit(kLaunchTimeout);
    ASSERT_TRUE(exitCode.has_value()) << "第二個執行個體沒有自行結束";
    EXPECT_EQ(*exitCode, 0u);

    EXPECT_TRUE(waitUntil([&] { return IsWindowVisible(lens_) != FALSE; }, kUiTimeout))
        << "第二個執行個體沒有通知第一個執行個體顯示透鏡";
    EXPECT_FALSE(app_->hasExited()) << "第一個執行個體被影響而結束了";
}

// IT-04：主程式結束後，單一執行個體的鎖要釋放，下一次可以正常啟動。
TEST_F(AppTest, InstanceCanStartAgainAfterExit) {
    ASSERT_EQ(app_->requestExit(kExitTimeout), std::optional<DWORD>{0});
    EXPECT_FALSE(AppProcess::isAnyInstanceRunning()) << "主程式結束後，單一執行個體的鎖沒有釋放";

    app_ = std::make_unique<AppProcess>(
        std::vector<std::wstring>{L"--data-dir", dataDirectory_.wstring()});
    lens_ = app_->waitForWindow(platform::LensWindow::kClassName, kLaunchTimeout);
    ASSERT_NE(lens_, nullptr) << "結束後無法再次啟動";
    EXPECT_TRUE(waitUntil([&] { return IsWindowVisible(lens_) != FALSE; }, kLaunchTimeout));
}

}  // namespace
}  // namespace tmw
