// 用過的翻譯引擎（設定視窗的「最近用過」、網頁漫畫控制面板的切換）。
//
// 每一筆是完整的引擎設定（包括加密過的金鑰，只存在使用者自己的設定檔），最近用的在前面。
// 網址、模型、區域都一樣就算同一筆。切換時只換掉引擎鏈的第一個，「失敗時改用 Google」照舊。
#pragma once

#include <cstddef>
#include <string>

#include "core/settings.h"

namespace tmw::core {

inline constexpr std::size_t kEngineHistoryLimit = 10;

// 把引擎鏈的第一個（現在用的）記到最前面。沒有引擎時不做事
void rememberCurrentEngine(Settings& settings);

// 給人看的名稱，例如「hy-mt2-tmw（Ollama 127.0.0.1:11434）」「Google 翻譯（免費）」
std::string engineLabel(const EngineSettings& engine);

// 改用歷史裡的第 index 筆當第一個引擎（金鑰一起帶過來），並把它移到歷史的最前面。
// 原本第一個後面還有 Google 的話照樣保留（失敗時改用 Google）；選的就是 Google 時引擎鏈只剩它。
// index 超出範圍時回傳 false、不改任何東西
bool useEngineFromHistory(Settings& settings, std::size_t index);

}  // namespace tmw::core
