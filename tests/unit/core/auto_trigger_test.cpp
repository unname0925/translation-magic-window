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

TEST_F(AutoTriggerTest, TheSettleTimeCanChangeWhileRunning) {
    // M2-06：切換情境模式時換等待時間（遊戲 600ms）
    trigger_->setSettleTime(600ms);
    frames_.setContent(solid(100));
    runFor(500ms);
    EXPECT_TRUE(requests_.empty()) << "還沒等滿 600ms";
    runFor(200ms);
    EXPECT_EQ(requests_.size(), 1u);
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

// M2-06：遊戲模式只看文字區域（design.md 4.3）。
// 透鏡範圍 400×300，縮圖 8×8：每個縮圖像素是 50×37.5 的一塊。
// 文字在左上角（0,0)～(150,75)，涵蓋縮圖的左上 3×2 格（加上留邊）。
// 速度優化 4：畫面停下 100ms 就先做 OCR，穩定了才處理（翻譯）
class PrepareTest : public ::testing::Test {
protected:
    PrepareTest() {
        AutoTrigger::Callbacks callbacks;
        callbacks.onProcess = [this](const ProcessRequest& request) {
            requests_.push_back(request);
            trigger_->onProcessingFinished(request.generation);
        };
        callbacks.onPrepare = [this](std::uint64_t ticket) {
            tickets_.push_back(ticket);
            preparedAt_.push_back(requests_.size());
        };
        trigger_.emplace(clock_, frames_, AutoTriggerConfig{400ms, {}}, std::move(callbacks));
        trigger_->onMoveSizeEnd(kRegion);
    }

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
    std::vector<std::uint64_t> tickets_;
    std::vector<std::size_t> preparedAt_;  // 發出 onPrepare 時已經處理過幾次
    std::optional<AutoTrigger> trigger_;
};

TEST_F(PrepareTest, PreparesBeforeTheScreenHasSettled) {
    frames_.setContent(solid(100));
    runFor(200ms);
    ASSERT_EQ(tickets_.size(), 1u) << "停下 100ms 就先做";
    EXPECT_TRUE(requests_.empty()) << "還沒等滿 400ms";
    runFor(400ms);
    ASSERT_EQ(requests_.size(), 1u);
    EXPECT_EQ(requests_[0].prepared, tickets_[0]) << "畫面沒變：處理時沿用預先做的 OCR";
    EXPECT_EQ(tickets_.size(), 1u) << "一輪只預先做一次";
}

TEST_F(PrepareTest, AChangeAfterPreparingVoidsTheTicket) {
    frames_.setContent(solid(100));
    runFor(200ms);
    ASSERT_EQ(tickets_.size(), 1u);
    frames_.setContent(solid(200));  // 預先做完之後畫面又變了
    runFor(600ms);
    ASSERT_EQ(tickets_.size(), 2u) << "停下來之後重新預先做";
    ASSERT_EQ(requests_.size(), 1u);
    EXPECT_EQ(requests_[0].prepared, tickets_[1]) << "用的是變了之後的那一次";
    EXPECT_NE(tickets_[0], tickets_[1]);
}

TEST_F(PrepareTest, MovingTheLensVoidsTheTicket) {
    frames_.setContent(solid(100));
    runFor(200ms);
    trigger_->onMoveSizeStart();
    trigger_->onMoveSizeEnd(RectI{0, 0, 300, 200});
    clock_.advance(100ms);
    trigger_->tick();  // 新位置的第一張縮圖
    runFor(500ms);
    ASSERT_EQ(requests_.size(), 1u);
    ASSERT_EQ(tickets_.size(), 2u);
    EXPECT_EQ(requests_[0].prepared, tickets_[1]);
}

TEST_F(PrepareTest, NothingIsPreparedForContentAlreadyProcessed) {
    frames_.setContent(solid(100));
    runFor(1s);
    frames_.setContent(solid(200));  // 閃了一下
    runFor(100ms);
    frames_.setContent(solid(100));  // 又回到處理過的樣子
    runFor(1s);
    EXPECT_EQ(tickets_.size(), 1u) << "到時候也不會處理，不用預先做";
    EXPECT_EQ(requests_.size(), 1u);
}

TEST_F(PrepareTest, NotWhenTheSettleTimeIsAlreadyShort) {
    trigger_->setSettleTime(100ms);
    frames_.setContent(solid(100));
    runFor(1s);
    EXPECT_TRUE(tickets_.empty()) << "等待時間和預先做的時間一樣短，直接處理就好";
    ASSERT_EQ(requests_.size(), 1u);
    EXPECT_EQ(requests_[0].prepared, 0u);
}

class GameModeTest : public ::testing::Test {
protected:
    GameModeTest() {
        AutoTrigger::Callbacks callbacks;
        callbacks.onProcess = [this](const ProcessRequest& request) {
            requests_.push_back(request);
            trigger_->onProcessingFinished(request.generation);
        };
        AutoTriggerConfig config{400ms, {}};
        config.focusOnText = true;
        trigger_.emplace(clock_, frames_, config, std::move(callbacks));
        frames_.setContent(scene(false, false));
        trigger_->onMoveSizeEnd(kRegion);
    }

    // text：台詞換了沒有；blink：右下角的游標亮或暗
    static GrayImage scene(bool text, bool blink) {
        GrayImage image(8, 8, 60);
        image.pixels[0] = text ? 200 : 255;          // 文字所在的縮圖像素
        image.pixels[7 * 8 + 7] = blink ? 250 : 60;  // 右下角的游標
        return image;
    }

    void runFor(std::chrono::milliseconds duration, bool animate = false) {
        bool blink = false;
        bool text = textChanged_;
        for (auto elapsed = 0ms; elapsed < duration; elapsed += 100ms) {
            if (animate && elapsed.count() % 300 == 0) {
                blink = !blink;  // 每 300ms 閃一次，比 settle 的 400ms 短
                frames_.setContent(scene(text, blink));
            }
            clock_.advance(100ms);
            trigger_->tick();
        }
    }

    static constexpr RectI kRegion{100, 100, 500, 400};
    test::FakeClock clock_;
    test::FakeFrameSource frames_;
    std::vector<ProcessRequest> requests_;
    bool textChanged_ = false;
    std::optional<AutoTrigger> trigger_;
};

TEST_F(GameModeTest, ABlinkingCursorKeepsANormalLensWaitingForever) {
    // 問題本身：沒有文字區域時（第一次處理之前，或不是遊戲模式），閃爍的游標讓畫面永遠「不穩定」
    runFor(3s, /*animate=*/true);
    EXPECT_TRUE(requests_.empty());
}

TEST_F(GameModeTest, ABlinkingCursorOutsideTheTextIsIgnored) {
    trigger_->setFocusRegions(std::vector<RectI>{{0, 0, 150, 75}});
    runFor(3s, /*animate=*/true);
    EXPECT_EQ(requests_.size(), 1u) << "游標不在文字區域裡，畫面算是穩定的";
}

TEST_F(GameModeTest, NewDialogueIsProcessedEvenWhileTheCursorBlinks) {
    trigger_->setFocusRegions(std::vector<RectI>{{0, 0, 150, 75}});
    runFor(1s, /*animate=*/true);
    ASSERT_EQ(requests_.size(), 1u);
    textChanged_ = true;  // 換下一句台詞
    runFor(2s, /*animate=*/true);
    EXPECT_EQ(requests_.size(), 2u);
}

TEST_F(GameModeTest, AWholeNewSceneIsProcessed) {
    // 文字區域外的大變化（換場景、跳出大對話框）：超過一半的縮圖像素變了
    trigger_->setFocusRegions(std::vector<RectI>{{0, 0, 150, 75}});
    runFor(1s);
    ASSERT_EQ(requests_.size(), 1u);
    GrayImage scene(8, 8, 200);
    scene.pixels[0] = 255;  // 文字區域本身沒變
    frames_.setContent(scene);
    runFor(1s);
    EXPECT_EQ(requests_.size(), 2u);
}

TEST_F(GameModeTest, SmallChangesOutsideTheTextDoNotRerunOcr) {
    // 「和上次處理過的一樣就不重跑」也只看文字區域，不然背景一動就重跑一次 OCR
    trigger_->setFocusRegions(std::vector<RectI>{{0, 0, 150, 75}});
    runFor(1s);
    ASSERT_EQ(requests_.size(), 1u);
    frames_.setContent(scene(false, true));  // 只有游標變了，然後停住
    runFor(1s);
    EXPECT_EQ(requests_.size(), 1u);
}

TEST_F(GameModeTest, MovingTheLensForgetsTheTextRegions) {
    trigger_->setFocusRegions(std::vector<RectI>{{0, 0, 150, 75}});
    trigger_->onMoveSizeStart();
    trigger_->onMoveSizeEnd(kRegion);
    runFor(3s, /*animate=*/true);
    EXPECT_TRUE(requests_.empty()) << "新位置的文字在哪還不知道，回到看整個畫面";
}

TEST_F(GameModeTest, FocusRegionsAreIgnoredOutsideGameMode) {
    AutoTrigger::Callbacks callbacks;
    callbacks.onProcess = [this](const ProcessRequest& request) { requests_.push_back(request); };
    AutoTrigger normal(clock_, frames_, AutoTriggerConfig{400ms, {}}, std::move(callbacks));
    normal.onMoveSizeEnd(kRegion);
    normal.setFocusRegions(std::vector<RectI>{{0, 0, 150, 75}});
    bool blink = false;
    for (auto elapsed = 0ms; elapsed < 3s; elapsed += 100ms) {
        if (elapsed.count() % 300 == 0) {
            blink = !blink;
            frames_.setContent(scene(false, blink));
        }
        clock_.advance(100ms);
        normal.tick();
    }
    EXPECT_TRUE(requests_.empty());
}

}  // namespace
}  // namespace tmw::core
