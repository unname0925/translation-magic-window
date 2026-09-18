#include "core/auto_trigger.h"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <vector>

#include "support/fake_clock.h"
#include "support/fake_frame_source.h"

namespace tmw::core {
namespace {

using namespace std::chrono_literals;

GrayImage solid(std::uint8_t value) {
    return GrayImage(8, 8, value);
}

class AutoTriggerTest : public ::testing::Test {
protected:
    AutoTriggerTest() {
        AutoTrigger::Callbacks callbacks;
        callbacks.onStateChanged = [this](LensState state) { states_.push_back(state); };
        callbacks.onProcess = [this](const ProcessRequest& request) {
            requests_.push_back(request);
            if (finishImmediately_) {
                trigger_->onProcessingFinished(request.generation);
            }
        };
        trigger_.emplace(clock_, frames_, AutoTriggerConfig{400ms, {}}, std::move(callbacks));
        trigger_->onMoveSizeEnd(kRegion);
    }

    // 模擬 app 的計時器：每 100ms 呼叫一次 tick
    void runFor(std::chrono::milliseconds duration) {
        for (auto elapsed = 0ms; elapsed < duration; elapsed += 100ms) {
            clock_.advance(100ms);
            trigger_->tick();
        }
    }

    static constexpr RectI kRegion{100, 100, 500, 400};
    test::FakeClock clock_;
    test::FakeFrameSource frames_;
    std::vector<ProcessRequest> requests_;
    std::vector<LensState> states_;
    bool finishImmediately_ = true;
    // 用 optional 延後建構，讓回呼可以參考已經建好的成員
    std::optional<AutoTrigger> trigger_;
};

TEST_F(AutoTriggerTest, ProcessesOnceAfterContentSettles) {
    frames_.setContent(solid(100));
    runFor(1s);
    ASSERT_EQ(requests_.size(), 1u);
    EXPECT_EQ(trigger_->state(), LensState::Showing);
    EXPECT_EQ(frames_.lastRegion(), kRegion);

    runFor(5s);
    EXPECT_EQ(requests_.size(), 1u) << "畫面沒變就不應該重複處理";
}

TEST_F(AutoTriggerTest, DoesNotResampleWhenScreenIsStatic) {
    frames_.setContent(solid(100));
    runFor(5s);
    EXPECT_EQ(frames_.thumbnailCalls(), 1) << "螢幕沒有新畫面時不應該取樣（閒置時不耗 CPU）";
}

TEST_F(AutoTriggerTest, ChangedContentIsProcessedAfterSettling) {
    frames_.setContent(solid(100));
    runFor(1s);
    ASSERT_EQ(requests_.size(), 1u);

    frames_.setContent(solid(200));
    runFor(100ms);
    EXPECT_EQ(trigger_->state(), LensState::Settling);
    runFor(200ms);
    EXPECT_EQ(requests_.size(), 1u) << "還沒穩定夠久";
    runFor(300ms);
    EXPECT_EQ(requests_.size(), 2u);
}

TEST_F(AutoTriggerTest, KeepsWaitingWhileContentKeepsChanging) {
    frames_.setContent(solid(0));
    for (int i = 1; i <= 20; ++i) {  // 2 秒內每 100ms 變一次
        frames_.setContent(solid(static_cast<std::uint8_t>(i * 10)));
        runFor(100ms);
    }
    EXPECT_TRUE(requests_.empty()) << "畫面一直在變，不應該觸發";
    runFor(500ms);
    EXPECT_EQ(requests_.size(), 1u);
}

TEST_F(AutoTriggerTest, ContentReturningToProcessedStateIsNotReprocessed) {
    frames_.setContent(solid(100));
    runFor(1s);
    ASSERT_EQ(requests_.size(), 1u);

    frames_.setContent(solid(200));  // 閃了一下
    runFor(100ms);
    frames_.setContent(solid(100));  // 又回到原樣
    runFor(1s);
    EXPECT_EQ(requests_.size(), 1u) << "內容和上次處理過的一樣，不用重做";
    EXPECT_EQ(trigger_->state(), LensState::Showing);
}

TEST_F(AutoTriggerTest, DraggingSuppressesProcessingUntilReleased) {
    frames_.setContent(solid(100));
    runFor(1s);
    ASSERT_EQ(requests_.size(), 1u);

    trigger_->onMoveSizeStart();
    for (int i = 0; i < 10; ++i) {
        frames_.setContent(solid(static_cast<std::uint8_t>(i * 20)));
        runFor(200ms);
    }
    EXPECT_EQ(requests_.size(), 1u);
    EXPECT_EQ(trigger_->state(), LensState::Dragging);

    const RectI moved{300, 300, 700, 600};
    frames_.setContent(solid(77));
    trigger_->onMoveSizeEnd(moved);
    runFor(1s);
    EXPECT_EQ(requests_.size(), 2u);
    EXPECT_EQ(frames_.lastRegion(), moved) << "放開後要取樣新的位置";
}

TEST_F(AutoTriggerTest, StaleResultIsRejected) {
    finishImmediately_ = false;
    frames_.setContent(solid(100));
    runFor(500ms);
    ASSERT_EQ(requests_.size(), 1u);
    EXPECT_EQ(trigger_->state(), LensState::Processing);

    frames_.setContent(solid(200));  // 處理到一半畫面變了
    runFor(100ms);
    EXPECT_FALSE(trigger_->onProcessingFinished(requests_[0].generation));
    EXPECT_EQ(trigger_->state(), LensState::Settling);
}

TEST_F(AutoTriggerTest, ManualTriggerIgnoresSettlingAndDuplicates) {
    frames_.setContent(solid(100));
    runFor(1s);
    ASSERT_EQ(requests_.size(), 1u);

    trigger_->manualTrigger();  // 內容沒變也要處理
    ASSERT_EQ(requests_.size(), 2u);
    EXPECT_TRUE(requests_[1].manual);
}

TEST_F(AutoTriggerTest, DisabledTriggerDoesNothing) {
    trigger_->setEnabled(false);
    frames_.setContent(solid(100));
    runFor(2s);
    trigger_->manualTrigger();
    EXPECT_TRUE(requests_.empty());
    EXPECT_EQ(frames_.thumbnailCalls(), 0);

    trigger_->setEnabled(true);
    runFor(1s);
    EXPECT_EQ(requests_.size(), 1u);
}

TEST_F(AutoTriggerTest, FailingFrameSourceStillProcessesAfterSettling) {
    // 取不到縮圖時無法偵測變化，但透鏡停下來後仍然要處理一次
    frames_.setFailing(true);
    runFor(1s);
    EXPECT_EQ(requests_.size(), 1u);
}

TEST_F(AutoTriggerTest, ReportsStateChanges) {
    frames_.setContent(solid(100));
    runFor(1s);
    trigger_->onMoveSizeStart();
    trigger_->onMoveSizeEnd(kRegion);
    const std::vector<LensState> expected{LensState::Processing, LensState::Showing,
                                          LensState::Dragging, LensState::Settling};
    EXPECT_EQ(states_, expected);
}

}  // namespace
}  // namespace tmw::core
