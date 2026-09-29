// 專有名詞表（M2-09，design.md 4.5「專有名詞表」）。
//
// 使用者在 glossary.txt 裡一行寫一個詞：「原文=譯文」（全形的＝也可以），# 開頭是註解。
// 例如角色名字「悠真=悠真」、招式「エクスカリバー=王者之劍」，LLM 就會照表翻，
// 不會同一個名字這頁翻「悠真」、下一頁翻「優馬」。
//
// 只把這次畫面裡真的出現的詞送出去：整張表可能有幾百個詞，全部送會拖慢翻譯、
// 也讓模型去注意根本不在畫面上的名字。
#pragma once

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tmw::core {

// 原文 -> 譯文
using Glossary = std::map<std::string, std::string>;

struct GlossaryLoad {
    Glossary entries;
    // 看不懂的行（「第 3 行沒有 =」），給記錄檔看；其他行照常讀取
    std::vector<std::string> problems;
};

// text 是 glossary.txt 的內容（UTF-8，開頭可以有 BOM）。同一個原文出現兩次時以後面的為準。
GlossaryLoad parseGlossary(std::string_view text);

// 在 segments 任何一段裡出現過的詞條。比對區分大小寫，而且是單純的子字串比對。
Glossary glossaryFor(std::span<const std::string> segments, const Glossary& glossary);

// 出現在 text 裡的詞條接成一個字串，放進翻譯快取的鍵：改了這段文字用到的詞才要重翻，
// 改了別的詞不影響。沒有用到任何詞時是空字串。
std::string glossaryFingerprint(std::string_view text, const Glossary& glossary);

// glossary.txt 第一次建立時的內容（說明怎麼寫）
std::string_view glossaryTemplate();

}  // namespace tmw::core
