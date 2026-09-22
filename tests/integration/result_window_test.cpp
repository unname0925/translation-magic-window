// 結果視窗（MT-UI 中可以自動化的部分）：卡片、捲動與「回到最新」、置頂、字級、位置還原。
#include "ui/result_window.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <QApplication>
#include <QListView>
#include <QScrollBar>
#include <chrono>
#include <string>

#include "core/history.h"

namespace tmw::ui {
namespace {

core::HistoryCard card(const std::string& source, int lens = 1) {
    core::HistoryCard out;
    out.lens = lens;
    out.language = core::Language::Japanese;
    out.time = std::chrono::system_clock::now();
    out.groups.push_back(core::HistoryGroup{source, "譯:" + source});
    return out;
}

// 讓 Qt 處理完排隊中的事件（版面、捲軸的範圍都是事件處理後才更新）
void settle() {
    for (int i = 0; i < 5; ++i) {
        QApplication::processEvents();
    }
}

class ResultWindowTest : public ::testing::Test {
protected:
    void SetUp() override {
        window_.resize(400, 300);
        window_.show();
        settle();
    }

    void TearDown() override {
        window_.hide();
        settle();
    }

    void fill(int count) {
        for (int i = 0; i < count; ++i) {
            window_.addCard(card("句子" + std::to_string(i)));
        }
        settle();
    }

    ResultWindow window_;
};

TEST_F(ResultWindowTest, ShowsTheCardsItIsGiven) {
    fill(3);
    EXPECT_EQ(window_.cardCount(), 3);
}

TEST_F(ResultWindowTest, ClearRemovesEverything) {
    fill(3);
    window_.clearCards();
    settle();
    EXPECT_EQ(window_.cardCount(), 0);
}

TEST_F(ResultWindowTest, StaysAtTheLatestWhileAtTheBottom) {
    fill(30);
    EXPECT_TRUE(window_.followingLatest()) << "沒有往上捲時要跟著最新的卡片";
}

TEST_F(ResultWindowTest, DoesNotJumpBackWhenTheUserScrolledUp) {
    fill(30);
    auto* view = window_.findChild<QListView*>();
    ASSERT_NE(view, nullptr);
    QScrollBar* bar = view->verticalScrollBar();
    ASSERT_NE(bar, nullptr);
    ASSERT_GT(view->sizeHintForRow(0), 0) << "每一張卡片都要有高度";
    ASSERT_GT(bar->maximum(), 0) << "卡片要多到會出現捲軸";

    bar->setValue(0);  // 使用者捲到最上面
    settle();
    EXPECT_FALSE(window_.followingLatest());

    const int before = bar->value();
    window_.addCard(card("新的句子"));
    settle();
    EXPECT_EQ(bar->value(), before) << "看歷史時不該被拉回底部";
}

TEST_F(ResultWindowTest, EveryCardHasAHeight) {
    // 高度是 delegate 依內容算出來的。算成 0 的話視窗會一片空白。
    fill(2);
    auto* view = window_.findChild<QListView*>();
    ASSERT_NE(view, nullptr);
    EXPECT_GT(view->sizeHintForRow(0), 0);
    EXPECT_GT(view->sizeHintForRow(1), 0);
}

TEST_F(ResultWindowTest, FontSizeStaysInAReadableRange) {
    window_.setFontPointSize(1);
    EXPECT_GE(window_.fontPointSize(), 7);
    window_.setFontPointSize(1000);
    EXPECT_LE(window_.fontPointSize(), 28);
}

TEST_F(ResultWindowTest, RemembersItsGeometry) {
    const core::RectI wanted = core::RectI::fromXYWH(120, 140, 360, 420);
    window_.restoreGeometry(wanted);
    settle();
    const core::RectI actual = window_.savedGeometry();
    EXPECT_EQ(actual.width(), wanted.width());
    EXPECT_EQ(actual.height(), wanted.height());
    EXPECT_EQ(actual.left, wanted.left);
    EXPECT_EQ(actual.top, wanted.top);
}

TEST_F(ResultWindowTest, IgnoresAnEmptyGeometry) {
    const core::RectI before = window_.savedGeometry();
    window_.restoreGeometry(core::RectI{});
    EXPECT_EQ(window_.savedGeometry(), before) << "還沒記過位置時不要動它";
}

TEST_F(ResultWindowTest, TogglesAlwaysOnTop) {
    EXPECT_FALSE(window_.alwaysOnTop());
    window_.setAlwaysOnTop(true);
    settle();
    EXPECT_TRUE(window_.alwaysOnTop());
    window_.setAlwaysOnTop(false);
    settle();
    EXPECT_FALSE(window_.alwaysOnTop());
}

TEST_F(ResultWindowTest, ReportsSettingsChangesAfterThingsSettleDown) {
    // 拖動視窗時每個像素都存一次設定太浪費，所以停下來之後才通知
    int changes = 0;
    QObject::connect(&window_, &ResultWindow::settingsChanged, [&changes] { ++changes; });
    window_.setFontPointSize(14);
    EXPECT_EQ(changes, 0) << "改的當下先不要存";

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (changes == 0 && std::chrono::steady_clock::now() < deadline) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    EXPECT_GT(changes, 0) << "停下來之後要通知一次";
}

TEST_F(ResultWindowTest, IsExcludedFromScreenCapture) {
    // 否則把透鏡拖到結果視窗上會翻譯自己的輸出（design.md 4.1）
    const auto handle = reinterpret_cast<HWND>(window_.winId());
    ASSERT_NE(handle, nullptr);
    DWORD affinity = 0;
    ASSERT_TRUE(GetWindowDisplayAffinity(handle, &affinity));
    EXPECT_EQ(affinity, static_cast<DWORD>(WDA_EXCLUDEFROMCAPTURE));
}

}  // namespace
}  // namespace tmw::ui
