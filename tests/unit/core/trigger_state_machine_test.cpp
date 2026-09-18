// UT-01：觸發狀態機（見 docs/execution-plan.md 5.3）
#include "core/trigger_state_machine.h"

#include <gtest/gtest.h>

#include <chrono>

#include "support/fake_clock.h"

namespace tmw::core {
namespace {

using namespace std::chrono_literals;

class TriggerStateMachineTest : public ::testing::Test {
protected:
    test::FakeClock clock_;
    TriggerStateMachine machine_{clock_, 400ms};

    // 讓狀態機走到「顯示中」，回傳那次處理的流水號
    std::uint64_t runUntilShowing() {
        clock_.advance(400ms);
        const auto request = machine_.poll();
        EXPECT_TRUE(request.has_value());
        EXPECT_TRUE(machine_.onProcessingFinished(request->generation));
        EXPECT_EQ(machine_.state(), LensState::Showing);
        return request->generation;
    }
};

TEST_F(TriggerStateMachineTest, StartsSettling) {
    EXPECT_EQ(machine_.state(), LensState::Settling);
    EXPECT_FALSE(machine_.poll().has_value());
}

TEST_F(TriggerStateMachineTest, ProcessesAfterSettleTime) {
    clock_.advance(399ms);
    EXPECT_FALSE(machine_.poll().has_value());
    clock_.advance(1ms);
    const auto request = machine_.poll();
    ASSERT_TRUE(request.has_value());
    EXPECT_FALSE(request->manual);
    EXPECT_EQ(machine_.state(), LensState::Processing);
    // 同一次只會發出一個請求
    clock_.advance(1s);
    EXPECT_FALSE(machine_.poll().has_value());
}

TEST_F(TriggerStateMachineTest, ContentChangeWhileSettlingRestartsTimer) {
    clock_.advance(300ms);
    machine_.onContentChanged();
    clock_.advance(300ms);
    EXPECT_FALSE(machine_.poll().has_value()) << "計時應該從畫面變化時重新開始";
    clock_.advance(100ms);
    EXPECT_TRUE(machine_.poll().has_value());
}

TEST_F(TriggerStateMachineTest, DraggingSuppressesEverything) {
    machine_.onMoveSizeStart();
    EXPECT_EQ(machine_.state(), LensState::Dragging);
    machine_.onContentChanged();  // 拖動中畫面一直在變
    clock_.advance(5s);
    EXPECT_FALSE(machine_.poll().has_value());
    EXPECT_FALSE(machine_.onManualTrigger().has_value()) << "拖動中不能手動觸發";
    EXPECT_EQ(machine_.state(), LensState::Dragging);
}

TEST_F(TriggerStateMachineTest, SettleTimerStartsWhenDragEnds) {
    machine_.onMoveSizeStart();
    clock_.advance(5s);
    machine_.onMoveSizeEnd();
    EXPECT_EQ(machine_.state(), LensState::Settling);
    clock_.advance(399ms);
    EXPECT_FALSE(machine_.poll().has_value());
    clock_.advance(1ms);
    EXPECT_TRUE(machine_.poll().has_value());
}

TEST_F(TriggerStateMachineTest, ContentChangeWhileProcessingCancelsIt) {
    clock_.advance(400ms);
    const auto request = machine_.poll();
    ASSERT_TRUE(request.has_value());

    machine_.onContentChanged();
    EXPECT_EQ(machine_.state(), LensState::Settling);
    EXPECT_FALSE(machine_.onProcessingFinished(request->generation)) << "過時的結果必須被丟棄";
    EXPECT_EQ(machine_.state(), LensState::Settling);

    clock_.advance(400ms);
    const auto retry = machine_.poll();
    ASSERT_TRUE(retry.has_value());
    EXPECT_NE(retry->generation, request->generation);
    EXPECT_TRUE(machine_.onProcessingFinished(retry->generation));
}

TEST_F(TriggerStateMachineTest, DragStartCancelsProcessing) {
    clock_.advance(400ms);
    const auto request = machine_.poll();
    ASSERT_TRUE(request.has_value());

    machine_.onMoveSizeStart();
    EXPECT_FALSE(machine_.onProcessingFinished(request->generation))
        << "舊位置的結果不能套用到新位置";
    EXPECT_EQ(machine_.state(), LensState::Dragging);
}

TEST_F(TriggerStateMachineTest, ContentChangeWhileShowingStartsSettling) {
    runUntilShowing();
    machine_.onContentChanged();
    EXPECT_EQ(machine_.state(), LensState::Settling);
    clock_.advance(400ms);
    EXPECT_TRUE(machine_.poll().has_value());
}

TEST_F(TriggerStateMachineTest, ShowingStaysQuietWithoutChanges) {
    runUntilShowing();
    clock_.advance(10s);
    EXPECT_FALSE(machine_.poll().has_value());
    EXPECT_EQ(machine_.state(), LensState::Showing);
}

TEST_F(TriggerStateMachineTest, ManualTriggerSkipsWaiting) {
    clock_.advance(10ms);
    const auto request = machine_.onManualTrigger();
    ASSERT_TRUE(request.has_value());
    EXPECT_TRUE(request->manual);
    EXPECT_EQ(machine_.state(), LensState::Processing);
}

TEST_F(TriggerStateMachineTest, ManualTriggerReplacesRunningProcessing) {
    clock_.advance(400ms);
    const auto automatic = machine_.poll();
    ASSERT_TRUE(automatic.has_value());
    const auto manual = machine_.onManualTrigger();
    ASSERT_TRUE(manual.has_value());
    EXPECT_FALSE(machine_.onProcessingFinished(automatic->generation));
    EXPECT_TRUE(machine_.onProcessingFinished(manual->generation));
}

TEST_F(TriggerStateMachineTest, GenerationsAreUnique) {
    std::uint64_t previous = machine_.generation();
    for (int i = 0; i < 5; ++i) {
        const std::uint64_t generation = runUntilShowing();
        EXPECT_GT(generation, previous);
        previous = generation;
        machine_.onContentChanged();
    }
}

TEST_F(TriggerStateMachineTest, FinishedTwiceIsRejected) {
    const std::uint64_t generation = runUntilShowing();
    EXPECT_FALSE(machine_.onProcessingFinished(generation));
}

}  // namespace
}  // namespace tmw::core
