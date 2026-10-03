#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

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
    // 遊戲模式（M2-06，design.md 4.3「只看文字區域」）：給了文字區域之後改成兩層偵測——
    // 文字區域內照原本的門檻（換台詞、打字機效果），整個範圍要有 largeChangeFraction 以上的
    // 縮圖像素變了才算（換場景、跳出大對話框）。閃爍的游標、角色待機動畫、局部的背景動態
    // 都不會再讓畫面永遠「不穩定」。整個畫面都在動的背景（捲動的風景）兩層都擋不住。
    bool focusOnText = false;
    double largeChangeFraction = 0.5;
    // 畫面停下這麼久就先開始 OCR（onPrepare），不必等滿 settleTime（速度優化 4，
    // docs/proposal-speed-and-web-manga.md）。之後畫面又變了，那次的結果就不用；
    // 翻譯一定等真的穩定了才送（付費引擎不會多花錢）。0 或不小於 settleTime 時不預先做
    Duration prepareAfter = std::chrono::milliseconds{100};
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
        // 畫面停下 prepareAfter：可以先做 OCR，記在這個票號下。之後畫面沒變的話，
        // onProcess 的 ProcessRequest::prepared 會是同一個票號。沒有設定就不預先做
        std::function<void(std::uint64_t ticket)> onPrepare;
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

    // 遊戲模式：上一次 OCR 讀到文字的位置（相對於擷取範圍左上角，也就是畫面座標）。
    // 空的代表沒讀到文字，回到看整個範圍。不是遊戲模式時忽略。透鏡移動後自動清掉。
    void setFocusRegions(std::span<const RectI> regions);

    // 畫面停下來多久才處理（切換情境模式時）
    void setSettleTime(Duration settleTime) { machine_.setSettleTime(settleTime); }

    // 執行中切換遊戲模式（設定視窗、系統匣）。關掉時一併清掉文字區域。
    void setFocusOnText(bool enabled);

private:
    // 兩張縮圖之間算不算「有變化」：一般是整張比；遊戲模式且有文字區域時用兩層規則
    bool changed(const GrayImage& before, const GrayImage& after);
    // 文字區域對應到這個大小的縮圖上，哪些像素要看（每個區域往外多留半個字高）
    const std::vector<std::uint8_t>& focusMask(int width, int height);

    void sample();
    // 畫面停下夠久、這一輪還沒預先做過：發出 onPrepare
    void maybePrepare();
    // 畫面變了或透鏡移動：預先做的 OCR 不能用了，下一輪重新預先做
    void forgetPrepared();
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

    std::uint64_t prepareSerial_ = 0;
    std::uint64_t preparedTicket_ = 0;  // 還有效的預先 OCR；0 表示沒有
    bool preparedThisRound_ = false;    // 這一輪等待穩定已經發過 onPrepare

    std::vector<RectI> focus_;
    std::vector<std::uint8_t> mask_;  // focusMask 的快取
    int maskWidth_ = 0;
    int maskHeight_ = 0;
};

}  // namespace tmw::core
