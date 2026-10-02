#include "core/auto_trigger.h"

#include <algorithm>
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
    // 新位置的內容和舊的無關：重新取樣，文字在哪也還不知道
    reference_.reset();
    focus_.clear();
    mask_.clear();
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
    if (changed(*reference_, *thumbnail)) {
        reference_ = std::move(*thumbnail);
        machine_.onContentChanged();
    }
}

void AutoTrigger::dispatch(const ProcessRequest& request) {
    if (lastProcessed_ && reference_ && !changed(*lastProcessed_, *reference_)) {
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

void AutoTrigger::setFocusRegions(std::span<const RectI> regions) {
    if (!config_.focusOnText) {
        return;
    }
    focus_.assign(regions.begin(), regions.end());
    mask_.clear();
}

void AutoTrigger::setFocusOnText(bool enabled) {
    config_.focusOnText = enabled;
    if (!enabled) {
        focus_.clear();
        mask_.clear();
    }
}

const std::vector<std::uint8_t>& AutoTrigger::focusMask(int width, int height) {
    if (!mask_.empty() && maskWidth_ == width && maskHeight_ == height) {
        return mask_;
    }
    maskWidth_ = width;
    maskHeight_ = height;
    mask_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    const double scaleX = static_cast<double>(region_.width()) / width;
    const double scaleY = static_cast<double>(region_.height()) / height;
    for (const RectI& text : focus_) {
        // 多留半個字高：打字機效果的字會長出去，下一句也可能稍微長一點
        const int pad = std::max(4, std::min(text.width(), text.height()) / 2);
        const int x0 = std::max(0, static_cast<int>((text.left - pad) / scaleX));
        const int y0 = std::max(0, static_cast<int>((text.top - pad) / scaleY));
        const int x1 = std::min(width - 1, static_cast<int>((text.right + pad) / scaleX));
        const int y1 = std::min(height - 1, static_cast<int>((text.bottom + pad) / scaleY));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                mask_[static_cast<std::size_t>(y) * width + x] = 1;
            }
        }
    }
    return mask_;
}

bool AutoTrigger::changed(const GrayImage& before, const GrayImage& after) {
    if (!config_.focusOnText || focus_.empty() || before.width != after.width ||
        before.height != after.height || before.pixels.empty() || region_.width() <= 0 ||
        region_.height() <= 0) {
        return contentChanged(before, after, config_.thresholds);
    }
    // 第一層：文字區域內，照原本的門檻
    const ChangeMetrics inText = measureChange(before, after, config_.thresholds.pixelDelta,
                                               focusMask(before.width, before.height));
    if (inText.changedPixels >= config_.thresholds.minChangedPixels ||
        inText.meanAbsDelta > config_.thresholds.meanDelta) {
        return true;
    }
    // 第二層：整個範圍，要大變化才算
    const ChangeMetrics whole = measureChange(before, after, config_.thresholds.pixelDelta);
    return whole.changedPixels >=
           config_.largeChangeFraction * static_cast<double>(before.pixels.size());
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
