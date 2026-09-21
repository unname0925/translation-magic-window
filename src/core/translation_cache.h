// 翻譯快取（見 docs/design.md 4.5 步驟 1）。
//
// 只存在記憶體中，以「引擎 + 來源語言 + 目標語言 + 正規化後的原文」為鍵，滿了就淘汰最久沒用到的。
// 一筆是一段（不是一整批），所以畫面上只有一個對話框改變時，其餘各段仍然命中。
// 存進去的譯文已經過 OpenCC，取出來就是最終結果。
#pragma once

#include <cstddef>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace tmw::core {

// 正規化原文，讓只差在空白的兩段共用同一筆快取：
// 去掉前後空白，中間連續的空白（含 tab、換行、全形空白 U+3000、不換行空白
// U+00A0）合併成一個半形空白，
// 並去掉零寬字元。大小寫不動（全大寫的遊戲介面和一般句子可能要翻得不一樣）。
std::string normalizeSource(std::string_view text);

struct TranslationKey {
    std::string engine;
    std::string srcLang;
    std::string dstLang;
    std::string text;  // 原文，還沒正規化

    friend bool operator==(const TranslationKey&, const TranslationKey&) = default;
};

// 可以同時被多個執行緒使用（所有透鏡共用同一份）。
class TranslationCache {
public:
    static constexpr std::size_t kDefaultCapacity = 10000;

    explicit TranslationCache(std::size_t capacity = kDefaultCapacity);

    // 命中時把這筆移到最新
    std::optional<std::string> get(const TranslationKey& key);
    void put(const TranslationKey& key, std::string translation);

    std::size_t size() const;
    std::size_t capacity() const noexcept { return capacity_; }
    void clear();

private:
    struct Entry {
        std::string key;
        std::string translation;
    };

    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::list<Entry> entries_;  // 前面是最近用到的
    std::unordered_map<std::string, std::list<Entry>::iterator> index_;
};

}  // namespace tmw::core
