// 名詞記憶：同一個名字在每一頁都用同一個譯名。
//
// 一頁一頁翻譯時，LLM 每次自己決定名字怎麼翻，同一章裡主角的名字可能有好幾種譯法
// （英文版 30 頁實測：出現在 19 段的名字，最常見的譯名只佔 68%）。
// 做法：每頁辨識完，從原文找出看起來像名字的詞（findTerms）；還沒記過的先一起翻譯一次、
// 記下來，之後每一頁翻譯時都當成專有名詞表送出去（core/glossary.h）。
// 依「範圍」分開記（網頁漫畫是網站），存在一個 JSON 檔，下一章、重開之後還在。
#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/glossary.h"

namespace tmw::core {

// 原文裡看起來像名字的詞（出現的順序，不重複）：
// - 日文：片假名連續 3 個字以上；
//   1～4 個漢字後面接「さん、くん、ちゃん、様、先輩、先生」等敬稱（只取漢字）
// - 英文：日文名字的羅馬拼音（整個字都能拆成日文的音節，例如 Tanaka、Haruka），
//   去掉 -san 這類後綴；常用的英文字不算
std::vector<std::string> findTerms(std::string_view text);

class TermMemory {
public:
    // file 是空的：只放在記憶體裡（測試、預覽工具）
    explicit TermMemory(std::filesystem::path file = {}, std::size_t maxPerScope = 2000);

    // 這些詞裡已經記過的（原文 → 譯名）
    Glossary lookup(const std::string& scope, std::span<const std::string> terms) const;
    // 還沒記過的
    std::vector<std::string> missing(const std::string& scope,
                                     std::span<const std::string> terms) const;
    // 記下譯名。已經記過的不改（好幾頁同時翻譯時，先記下的為準）；譯名是空的不記
    void remember(const std::string& scope, const std::map<std::string, std::string>& terms);

    std::size_t size(const std::string& scope) const;

private:
    void load();
    void save() const;

    std::filesystem::path file_;
    std::size_t maxPerScope_;
    mutable std::mutex mutex_;
    // 範圍 → 原文 → 譯名；另外記先後，滿了先丟最早記的
    std::map<std::string, std::map<std::string, std::string>> terms_;
    std::map<std::string, std::vector<std::string>> order_;
};

}  // namespace tmw::core
