// 結果視窗中卡片的文字（見 docs/design.md 4.7）。
//
// 抽出來是為了能單獨測試：格式錯了不必開視窗才看得出來。
#pragma once

#include <QString>
#include <chrono>

#include "core/history.h"

namespace tmw::ui {

// 語言的顯示名稱：「日文」「英文」「韓文」；判斷不出來時回傳「未知語言」
QString languageName(core::Language language);

// 卡片的標題：「14:32:05 · 透鏡 1 · 日文」。時間用當地時間。
QString cardHeader(const core::HistoryCard& card);

}  // namespace tmw::ui
