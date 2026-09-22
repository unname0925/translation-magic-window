// 翻譯服務：把快取、引擎鏈和簡轉繁串起來（見 docs/design.md 4.5「處理流程」）。
//
//   查快取 → 引擎鏈（含備援）→ 對齊檢查 → OpenCC（s2twp）
//
// 所有透鏡共用同一個服務（design.md 3.1），所以它可以同時被多個執行緒呼叫。
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "core/text_converter.h"
#include "core/translation_cache.h"
#include "core/translator.h"
#include "core/translator_chain.h"

namespace tmw::core {

// 這一段需不需要送去翻譯。純數字、時間、符號（"110/110"、"00:35"）沒有字母，
// 翻了也是原樣回來，不如省下時間和費用。
bool needsTranslation(std::string_view text);

class TranslationService {
public:
    TranslationService(std::shared_ptr<TranslatorChain> chain,
                       std::shared_ptr<const ITextConverter> converter,
                       std::size_t cacheCapacity = TranslationCache::kDefaultCapacity);

    // 回傳和 segments 等長的譯文。不需要翻譯的段落原樣回傳。
    // 全部引擎都失敗時丟出 TranslatorError（呼叫端顯示錯誤，原文照樣顯示）。
    std::vector<std::string> translate(std::span<const std::string> segments,
                                       const TranslateRequest& request, std::stop_token cancel);

    TranslationCache& cache() { return cache_; }

    // 引擎鏈目前的狀況（除錯傾印用）
    std::string engineStatus() const;

private:
    std::shared_ptr<TranslatorChain> chain_;
    std::shared_ptr<const ITextConverter> converter_;
    TranslationCache cache_;
    std::vector<std::string> engineIds_;
};

}  // namespace tmw::core
