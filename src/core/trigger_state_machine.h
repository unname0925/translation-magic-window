#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "core/clock.h"

namespace tmw::core {

// 透鏡的觸發狀態（見 docs/design.md 4.3）。
enum class LensState {
    Dragging,    // 使用者正在拖動或縮放透鏡
    Settling,    // 等待畫面穩定
    Processing,  // 正在處理（擷取、OCR、翻譯）
    Showing,     // 處理完成，顯示結果中
};

std::string_view lensStateName(LensState state);

struct ProcessRequest {
    std::uint64_t generation = 0;  // 流水號；處理完成時要帶回來，用來判斷結果是否已經過時
    bool manual = false;           // 是否由快捷鍵手動觸發
};

// 觸發狀態機：只負責狀態轉換和流水號，不碰畫面或時間以外的任何東西。
//
//   拖動中 ──放開──▶ 等待穩定 ──穩定 settleTime──▶ 處理中 ──完成──▶ 顯示中
//                    ▲   │ 畫面變化：重新計時      │                  │
//                    │   └────────────────────────┘                  │
//                    ├── 畫面變化：取消這次處理（流水號加一）──────────┤
//                    └────────────────── 畫面變化 ───────────────────┘
//   開始拖動（任何狀態）：進入拖動中，流水號加一
//   手動觸發（拖動中以外）：立刻進入處理中
//
// 每次開始處理，以及每次取消處理，流水號都會加一。
// 所以處理完成時帶回的流水號和目前的不同，就代表結果已經過時，應該丟棄。
class TriggerStateMachine {
public:
    TriggerStateMachine(const IClock& clock, Duration settleTime);

    LensState state() const { return state_; }
    std::uint64_t generation() const { return generation_; }

    void onMoveSizeStart();
    void onMoveSizeEnd();
    void onContentChanged();

    // 定期呼叫。畫面已經穩定夠久時，進入處理中並回傳要處理的工作。
    std::optional<ProcessRequest> poll();

    // 手動觸發：拖動中以外的任何狀態都立刻開始處理（會取消進行中的處理）。
    std::optional<ProcessRequest> onManualTrigger();

    // 處理完成。回傳 false 代表這次的結果已經過時（期間畫面變了或透鏡被拖動），應該丟棄。
    bool onProcessingFinished(std::uint64_t generation);

private:
    void enterSettling();
    ProcessRequest startProcessing(bool manual);

    const IClock& clock_;
    Duration settleTime_;
    LensState state_ = LensState::Settling;
    std::uint64_t generation_ = 0;
    TimePoint settlingSince_;
};

}  // namespace tmw::core
