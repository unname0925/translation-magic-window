#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "core/change_detection.h"
#include "core/clock.h"
#include "core/frame_source.h"
#include "core/geometry.h"
#include "core/image.h"
#include "core/trigger_state_machine.h"

namespace tmw::core {

struct AutoTriggerConfig {
    Duration settleTime = std::chrono::milliseconds{400};
    ChangeThresholds thresholds;
};

// 自動觸發：定期取樣透鏡範圍的縮圖，偵測變化，畫面穩定後發出處理請求（見 docs/design.md 4.3）。
//
// - 螢幕沒有新畫面時（frameSerial 沒變）不取樣，閒置時幾乎不耗 CPU。
// - 內容和上一次處理過的一樣時（例如畫面閃了一下又回到原樣），不重複處理。
// - 所有方法都應該從同一個執行緒呼叫。回呼中可以直接呼叫 onProcessingFinished。
class AutoTrigger {
public:
    struct Callbacks {
        std::function<void(LensState)> onStateChanged;
        std::function<void(const ProcessRequest&)> onProcess;
    };

    AutoTrigger(const IClock& clock, IFrameSource& frames, AutoTriggerConfig config,
                Callbacks callbacks);

    LensState state() const { return machine_.state(); }

    // 透鏡開始拖動或縮放
    void onMoveSizeStart();
    // 放開滑鼠。newRegion 是新的擷取範圍（螢幕座標）。也用在第一次設定範圍。
    void onMoveSizeEnd(const RectI& newRegion);

    // 停用時（例如透鏡被隱藏）完全不取樣、不觸發。重新啟用時從「等待穩定」開始。
    void setEnabled(bool enabled);

    // 定期呼叫（例如每 100ms）
    void tick();

    // 快捷鍵手動觸發：不等穩定，也不管內容是否和上次相同
    void manualTrigger();

    // 處理完成。回傳 false 代表結果已經過時，應該丟棄。
    bool onProcessingFinished(std::uint64_t generation);

private:
    void sample();
    void dispatch(const ProcessRequest& request);
    void notifyIfStateChanged();

    IFrameSource& frames_;
    AutoTriggerConfig config_;
    Callbacks callbacks_;
    TriggerStateMachine machine_;
    LensState reportedState_;

    RectI region_;
    bool enabled_ = true;
    std::optional<std::uint64_t> lastSerial_;
    // 最後一次判定「有變化」時的縮圖。每次都和它比較，而不是和上一張比較，
    // 這樣緩慢的漸變累積到一定程度也會被偵測到。
    std::optional<GrayImage> reference_;
    std::optional<GrayImage> lastProcessed_;
};

}  // namespace tmw::core
