#include "core/translation_service.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "core/language.h"

namespace tmw::core {

bool needsTranslation(std::string_view text) {
    return detectLanguage(text) != Language::Unknown;
}

TranslationService::TranslationService(std::shared_ptr<TranslatorChain> chain,
                                       std::shared_ptr<const ITextConverter> converter,
                                       std::size_t cacheCapacity)
    : chain_(std::move(chain)), converter_(std::move(converter)), cache_(cacheCapacity) {
    engineIds_ = chain_->engineIds();
}

std::string TranslationService::engineStatus() const {
    return chain_ == nullptr ? std::string("（沒有引擎）") : chain_->describeEngines();
}

std::vector<std::string> TranslationService::translate(std::span<const std::string> segments,
                                                       const TranslateRequest& request,
                                                       std::stop_token cancel) {
    std::vector<std::string> out(segments.size());
    std::vector<std::string> misses;                             // 要送出去的原文
    std::vector<std::vector<std::size_t>> missTargets;           // 每一段對應的輸出位置
    std::unordered_map<std::string, std::size_t> alreadyQueued;  // 正規化後的原文 -> misses 的索引

    for (std::size_t i = 0; i < segments.size(); ++i) {
        const std::string& text = segments[i];
        if (!needsTranslation(text)) {
            out[i] = text;
            continue;
        }
        // 依照引擎鏈的順序找快取：首選引擎翻過的結果優先，它暫停時才用備援引擎留下的。
        bool cached = false;
        for (const std::string& engine : engineIds_) {
            if (std::optional<std::string> hit =
                    cache_.get(TranslationKey{engine, request.srcLang, request.dstLang, text})) {
                out[i] = std::move(*hit);
                cached = true;
                break;
            }
        }
        if (cached) {
            continue;
        }
        // 同一個畫面上重複的文字（例如兩個一樣的按鈕）只送一次
        const auto [entry, inserted] =
            alreadyQueued.try_emplace(normalizeSource(text), misses.size());
        if (inserted) {
            misses.push_back(text);
            missTargets.emplace_back();
        }
        missTargets[entry->second].push_back(i);
    }

    if (misses.empty()) {
        return out;
    }

    ChainResult result = chain_->translate(misses, request, cancel);
    for (std::size_t i = 0; i < misses.size(); ++i) {
        std::string translation = converter_->convert(result.translations[i]);
        cache_.put(TranslationKey{result.engine, request.srcLang, request.dstLang, misses[i]},
                   translation);
        for (const std::size_t target : missTargets[i]) {
            out[target] = translation;
        }
    }
    return out;
}

}  // namespace tmw::core
