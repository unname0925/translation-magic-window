// 引擎鏈（見 docs/design.md 4.5 步驟 2、3）。
//
// 依照設定的順序嘗試各引擎，遇到網路錯誤、限流、格式錯誤、數量對不上就改用下一個。
// 同一個引擎連續失敗 3 次就暫時跳過 5 分鐘，免得每一次翻譯都要先等它逾時。
#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "core/clock.h"
#include "core/translator.h"

namespace tmw::core {

struct ChainOptions {
    int failuresBeforePause = 3;
    Duration pause = std::chrono::minutes(5);
};

struct ChainResult {
    std::string engine;  // 實際翻出結果的引擎，用來當快取的鍵
    std::vector<std::string> translations;
};

// 可以同時被多個執行緒使用（所有透鏡共用同一條鏈）。呼叫引擎時不會握著鎖。
class TranslatorChain {
public:
    TranslatorChain(std::vector<std::shared_ptr<ITranslator>> engines, const IClock& clock,
                    ChainOptions options = {});

    // 成功時回傳和 segments 等長的譯文。全部引擎都失敗時丟出最後一個錯誤；
    // 全部都在暫停中時丟出 TranslateError::Unavailable。
    ChainResult translate(std::span<const std::string> segments, const TranslateRequest& request,
                          std::stop_token cancel);

    std::vector<std::string> engineIds() const;

    // 這個引擎現在是不是在暫停中（給設定畫面顯示狀態用）
    bool paused(std::string_view id) const;

private:
    struct State {
        std::shared_ptr<ITranslator> engine;
        int failures = 0;
        TimePoint pausedUntil{};
        // 為什麼被暫停。使用者看到「沒有可用的引擎」時才知道是被限流還是連不上。
        TranslateError lastError = TranslateError::Unavailable;
    };

    // 「google：被限流或額度用完，約 4 分鐘後再試」
    std::string describePaused() const;

    // 呼叫端要先取得 mutex_
    void recordFailure(State& state);

    const IClock& clock_;
    ChainOptions options_;
    mutable std::mutex mutex_;
    std::vector<State> states_;
};

}  // namespace tmw::core
