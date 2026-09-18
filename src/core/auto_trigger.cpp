#include "core/auto_trigger.h"

#include <utility>

namespace tmw::core {

AutoTrigger::AutoTrigger(const IClock& clock, IFrameSource& frames, AutoTriggerConfig config,
                         Callbacks callbacks)
    : frames_(frames),
      config_(config),
      callbacks_(std::move(callbacks)),
      machine_(clock, config.settleTime),
      reportedState_(machine_.state()) {}

void AutoTrigger::onMoveSizeStart() {
    machine_.onMoveSizeStart();
    notifyIfStateChanged();
}

void AutoTrigger::onMoveSizeEnd(const RectI& newRegion) {
    region_ = newRegion;
    // 新位置的內容和舊的無關：重新取樣
    reference_.reset();
    lastSerial_.reset();
    machine_.onMoveSizeEnd();
    notifyIfStateChanged();
}

void AutoTrigger::setEnabled(bool enabled) {
    if (enabled == enabled_) {
        return;
    }
    enabled_ = enabled;
    if (enabled_) {
        onMoveSizeEnd(region_);
    }
}

void AutoTrigger::tick() {
    if (!enabled_ || machine_.state() == LensState::Dragging) {
        return;
    }
    sample();
    const std::optional<ProcessRequest> request = machine_.poll();
    notifyIfStateChanged();
    if (request) {
        dispatch(*request);
    }
}

void AutoTrigger::manualTrigger() {
    if (!enabled_) {
        return;
    }
    const std::optional<ProcessRequest> request = machine_.onManualTrigger();
    notifyIfStateChanged();
    if (request) {
        lastProcessed_ = reference_;
        if (callbacks_.onProcess) {
            callbacks_.onProcess(*request);
        }
    }
}

bool AutoTrigger::onProcessingFinished(std::uint64_t generation) {
    const bool current = machine_.onProcessingFinished(generation);
    notifyIfStateChanged();
    return current;
}

void AutoTrigger::sample() {
    const std::uint64_t serial = frames_.frameSerial();
    if (lastSerial_ && *lastSerial_ == serial) {
        return;  // 螢幕沒有新畫面
    }
    std::optional<GrayImage> thumbnail = frames_.thumbnail(region_);
    if (!thumbnail) {
        return;  // 失敗時不記住序號，下次再試
    }
    lastSerial_ = serial;
    if (!reference_) {
        // 移動後的第一張：當作比較基準。計時已經在放開滑鼠時重新開始了。
        reference_ = std::move(*thumbnail);
        return;
    }
    if (contentChanged(*reference_, *thumbnail, config_.thresholds)) {
        reference_ = std::move(*thumbnail);
        machine_.onContentChanged();
    }
}

void AutoTrigger::dispatch(const ProcessRequest& request) {
    if (lastProcessed_ && reference_ &&
        !contentChanged(*lastProcessed_, *reference_, config_.thresholds)) {
        // 內容和上一次處理過的一樣，不用重做
        machine_.onProcessingFinished(request.generation);
        notifyIfStateChanged();
        return;
    }
    lastProcessed_ = reference_;
    if (callbacks_.onProcess) {
        callbacks_.onProcess(request);
    }
}

void AutoTrigger::notifyIfStateChanged() {
    const LensState state = machine_.state();
    if (state == reportedState_) {
        return;
    }
    reportedState_ = state;
    if (callbacks_.onStateChanged) {
        callbacks_.onStateChanged(state);
    }
}

}  // namespace tmw::core
