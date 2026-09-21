#include "core/translation_cache.h"

#include <algorithm>
#include <utility>

#include "core/utf8.h"

namespace tmw::core {
namespace {

bool isSpace(char32_t c) {
    return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == U'\f' || c == U'\v' ||
           c == 0x00A0 ||                   // 不換行空白
           (c >= 0x2000 && c <= 0x200A) ||  // 各種寬度的空白
           c == 0x3000;                     // 全形空白
}

bool isIgnorable(char32_t c) {
    return c == 0x200B || c == 0x200C || c == 0x200D ||  // 零寬空白、零寬非連字、零寬連字
           c == 0xFEFF;                                  // BOM
}

// 鍵的各欄位之間放一個不會出現在文字裡的位元組，避免 "a"+"bc" 和 "ab"+"c" 撞在一起。
constexpr char kSeparator = '\x1F';

std::string makeKey(const TranslationKey& key) {
    std::string out;
    out.reserve(key.engine.size() + key.srcLang.size() + key.dstLang.size() + key.text.size() + 4);
    out += key.engine;
    out += kSeparator;
    out += key.srcLang;
    out += kSeparator;
    out += key.dstLang;
    out += kSeparator;
    out += normalizeSource(key.text);
    return out;
}

}  // namespace

std::string normalizeSource(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (std::size_t i = 0; i < text.size();) {
        const char32_t c = nextCodePoint(text, i);
        if (isIgnorable(c)) {
            continue;
        }
        if (isSpace(c)) {
            pendingSpace = !out.empty();  // 前面還沒有字就不用留空白
            continue;
        }
        if (pendingSpace) {
            out.push_back(' ');
            pendingSpace = false;
        }
        appendCodePoint(out, c);
    }
    return out;
}

TranslationCache::TranslationCache(std::size_t capacity)
    : capacity_(std::max<std::size_t>(capacity, 1)) {}

std::optional<std::string> TranslationCache::get(const TranslationKey& key) {
    const std::string full = makeKey(key);
    const std::lock_guard lock(mutex_);
    const auto found = index_.find(full);
    if (found == index_.end()) {
        return std::nullopt;
    }
    entries_.splice(entries_.begin(), entries_, found->second);
    return found->second->translation;
}

void TranslationCache::put(const TranslationKey& key, std::string translation) {
    const std::string full = makeKey(key);
    const std::lock_guard lock(mutex_);
    const auto found = index_.find(full);
    if (found != index_.end()) {
        found->second->translation = std::move(translation);
        entries_.splice(entries_.begin(), entries_, found->second);
        return;
    }
    entries_.push_front(Entry{full, std::move(translation)});
    index_.emplace(full, entries_.begin());
    while (entries_.size() > capacity_) {
        index_.erase(entries_.back().key);
        entries_.pop_back();
    }
}

std::size_t TranslationCache::size() const {
    const std::lock_guard lock(mutex_);
    return entries_.size();
}

void TranslationCache::clear() {
    const std::lock_guard lock(mutex_);
    entries_.clear();
    index_.clear();
}

}  // namespace tmw::core
