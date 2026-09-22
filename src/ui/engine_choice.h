// 設定畫面上的「翻譯引擎」和設定檔中的引擎鏈怎麼對應（見 docs/design.md 4.5）。
//
// 設定檔存的是一串引擎（順序就是引擎鏈），但使用者要填的其實只有
// 「用 Google 還是 LLM」「LLM 的網址、模型、金鑰」「失敗時要不要退回 Google」。
// 這裡是純邏輯，不依賴 Qt，才能單獨測試。
#pragma once

#include <string>
#include <vector>

#include "core/settings.h"

namespace tmw::ui {

struct EngineChoice {
    // false：只用 Google（免費、不用金鑰，但用多了會被限流）
    bool useLlm = false;
    std::string endpoint;  // 例如 http://127.0.0.1:11434/v1
    std::string model;     // 例如 hy-mt2
    // LLM 失敗時改用 Google
    bool fallbackToGoogle = true;
    // 已經存了金鑰（畫面上顯示「已設定」，不會把金鑰讀出來）
    bool hasKey = false;
};

// 從設定檔讀出畫面要顯示的內容
EngineChoice engineChoiceFrom(const core::Settings& settings);

// 把畫面上的內容寫回引擎鏈。
// encryptedKey 是要存進去的金鑰（已經加密）；空字串代表沿用原本的。
std::vector<core::EngineSettings> enginesFor(const EngineChoice& choice,
                                             const core::Settings& current,
                                             const std::string& encryptedKey);

}  // namespace tmw::ui
