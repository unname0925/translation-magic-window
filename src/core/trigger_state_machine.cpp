#include "core/trigger_state_machine.h"

namespace tmw::core {

std::string_view lensStateName(LensState state) {
    switch (state) {
        case LensState::Dragging:
            return "Dragging";
        case LensState::Settling:
            return "Settling";
        case LensState::Processing:
            return "Processing";
        case LensState::Showing:
            return "Showing";
    }
    return "Unknown";
}

TriggerStateMachine::TriggerStateMachine(const IClock& clock, Duration settleTime)
    : clock_(clock), settleTime_(settleTime), settlingSince_(clock.now()) {}

void TriggerStateMachine::onMoveSizeStart() {
    ++generation_;  // 取消進行中的處理：結果會套用到舊的位置
    state_ = LensState::Dragging;
}

void TriggerStateMachine::onMoveSizeEnd() {
    enterSettling();
}

void TriggerStateMachine::onContentChanged() {
    switch (state_) {
        case LensState::Dragging:
            // 拖動中畫面本來就一直在變，放開後才開始計時
            break;
        case LensState::Settling:
            settlingSince_ = clock_.now();
            break;
        case LensState::Processing:
            ++generation_;
            enterSettling();
            break;
        case LensState::Showing:
            enterSettling();
            break;
    }
}

std::optional<ProcessRequest> TriggerStateMachine::poll() {
    if (state_ == LensState::Settling && clock_.now() - settlingSince_ >= settleTime_) {
        return startProcessing(false);
    }
    return std::nullopt;
}

std::optional<ProcessRequest> TriggerStateMachine::onManualTrigger() {
    if (state_ == LensState::Dragging) {
        return std::nullopt;
    }
    return startProcessing(true);
}

bool TriggerStateMachine::onProcessingFinished(std::uint64_t generation) {
    if (state_ != LensState::Processing || generation != generation_) {
        return false;
    }
    state_ = LensState::Showing;
    return true;
}

void TriggerStateMachine::enterSettling() {
    state_ = LensState::Settling;
    settlingSince_ = clock_.now();
}

ProcessRequest TriggerStateMachine::startProcessing(bool manual) {
    ++generation_;
    state_ = LensState::Processing;
    return {generation_, manual};
}

}  // namespace tmw::core
