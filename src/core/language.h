// 判斷一段文字是哪一種語言（見 docs/design.md 4.4「語言判斷」）。
//
// 只分辨產品支援的三種來源語言：
// - 有韓文字母 → 韓文
// - 有假名或漢字 → 日文（來源語言不包含中文，所以漢字一律當作日文）
// - 其餘有拉丁字母 → 英文
// 都沒有（純數字、符號）→ Unknown，交給呼叫端決定（通常沿用上一次的語言）。
#pragma once

#include <string>
#include <string_view>

namespace tmw::core {

enum class Language {
    Unknown,
    Japanese,
    English,
    Korean,
};

// "ja" | "en" | "ko" | ""（Unknown）
std::string languageCode(Language language);

Language detectLanguage(std::string_view utf8);

// 各語言有多少字（用來判斷混合文字裡哪一種比較多）
struct ScriptCounts {
    int kana = 0;    // 平假名、片假名
    int han = 0;     // 漢字
    int hangul = 0;  // 韓文字母
    int latin = 0;   // 拉丁字母
    int other = 0;   // 數字、標點、空白等

    friend bool operator==(const ScriptCounts&, const ScriptCounts&) = default;
};

ScriptCounts countScripts(std::string_view utf8);

}  // namespace tmw::core
