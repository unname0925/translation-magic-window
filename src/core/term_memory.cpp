#include "core/term_memory.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <system_error>

#include "core/utf8.h"

namespace tmw::core {
namespace {

bool isKatakana(char32_t c) {
    return (c >= 0x30A1 && c <= 0x30FA) || c == 0x30FC;  // 長音「ー」也算
}

bool isKanji(char32_t c) {
    return (c >= 0x4E00 && c <= 0x9FFF) || c == 0x3005;  // 「々」也算
}

// 名字後面的敬稱（日文）
constexpr std::u32string_view kHonorifics[] = {U"さん", U"くん", U"君",   U"ちゃん",
                                               U"様",   U"先輩", U"先生", U"殿"};

// 不是名字的常用英文字（漫畫的英文多半全大寫，不能靠大寫判斷）。只放會被當成「像羅馬拼音」的字
const std::set<std::string>& commonEnglish() {
    // 長得像日文拼音的常用英文字（英文的 l、c、v 不在日文拼音裡，所以要排除的字有限）
    static const std::set<std::string> words = {
        "age",   "ago",    "anime",  "are",    "ate",   "bake", "banana",  "bate",    "bee",
        "bite",  "bone",   "bore",   "bra",    "care",  "cute", "dare",    "date",    "demo",
        "dine",  "done",   "dose",   "dune",   "fade",  "fake", "fate",    "fee",     "fine",
        "fire",  "free",   "futon",  "gate",   "gone",  "hate", "here",    "hero",    "hide",
        "hire",  "hobo",   "home",   "hose",   "huge",  "joke", "june",    "karaoke", "karate",
        "kate",  "kimono", "kite",   "made",   "make",  "mama", "mane",    "manga",   "mate",
        "memo",  "menu",   "mike",   "mine",   "more",  "mute", "name",    "nana",    "nine",
        "ninja", "none",   "nose",   "note",   "one",   "pane", "papa",    "pea",     "pee",
        "pie",   "poke",   "pose",   "potato", "ramen", "rare", "rate",    "ride",    "ripe",
        "rise",  "robe",   "rode",   "rose",   "rude",  "safe", "sake",    "same",    "samurai",
        "sane",  "sea",    "see",    "seen",   "side",  "site", "some",    "sore",    "sumo",
        "sure",  "sushi",  "take",   "tame",   "tea",   "tee",  "tie",     "time",    "tire",
        "toe",   "tofu",   "tomato", "tone",   "too",   "tree", "tsunami", "tube",    "tune",
        "use",   "wade",   "wage",   "wake",   "ware",  "were", "where",   "wide",    "wife",
        "wine",  "wipe",   "wire",   "wise",   "woke",  "zero", "zone",
    };
    return words;
}

// 整個字都能拆成日文的音節（羅馬拼音）
bool looksLikeRomaji(const std::string& word) {
    static const std::regex romaji("^(?:(?:[kgsztdnhbpmrwfj]y?|ch|sh|ts|y)?[aiueo]|n)+$");
    return std::regex_match(word, romaji);
}

void addEnglishTerms(std::string_view text, std::vector<std::string>& out,
                     std::set<std::string>& seen) {
    std::size_t i = 0;
    while (i < text.size()) {
        if (!std::isalpha(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < text.size() && (std::isalpha(static_cast<unsigned char>(text[end])) ||
                                     text[end] == '-' || text[end] == '\'')) {
            ++end;
        }
        // 前後接著 @ _ . / 或數字：帳號、網址、檔名的一部分（@azu_knzm 的 azu），不是名字
        const auto glued = [](char c) {
            return c == '@' || c == '_' || c == '.' || c == '/' || c == '#' ||
                   std::isdigit(static_cast<unsigned char>(c));
        };
        if ((i > 0 && glued(text[i - 1])) || (end < text.size() && glued(text[end]))) {
            i = end;
            continue;
        }
        std::string word(text.substr(i, end - i));
        i = end;
        // 去掉 -san、-kun 這類後綴和所有格
        static const std::regex suffix("-(?:san|kun|chan|sama|senpai|sensei|dono)$|'s$",
                                       std::regex::icase);
        word = std::regex_replace(word, suffix, "");
        std::string lower;
        for (const char c : word) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (lower.size() < 3 || lower.find_first_of("-'") != std::string::npos ||
            commonEnglish().contains(lower) || !looksLikeRomaji(lower)) {
            continue;
        }
        // 大小寫不同算同一個詞，保留第一次出現的寫法（送給專有名詞表時要和原文一樣才對得上）
        if (seen.insert(lower).second) {
            out.push_back(word);
        }
    }
}

// 記憶裡的鍵：英文不分大小寫（漫畫的英文常常全大寫），日文照原樣
std::string termKey(const std::string& term) {
    if (!std::all_of(term.begin(), term.end(),
                     [](char c) { return static_cast<unsigned char>(c) < 0x80; })) {
        return term;
    }
    std::string key;
    for (const char c : term) {
        key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return key;
}

}  // namespace

bool isUsableTermTranslation(std::string_view translation) {
    if (translation.empty()) {
        return false;
    }
    // 模型照抄了提示詞裡的說明用語（ルビ 的「{本文|讀音}」、專有名詞表）：「讀音：ミツキ」「本文」
    for (const std::string_view echo :
         {"本文", "讀音", "譯者", "原文", "譯文", ":", "：", "{", "|"}) {
        if (translation.find(echo) != std::string_view::npos) {
            return false;
        }
    }
    int count = 0;
    for (std::size_t i = 0; i < translation.size();) {
        const char32_t c = nextCodePoint(translation, i);
        ++count;
        if ((c >= 0x3041 && c <= 0x309F) || (c >= 0x30A1 && c <= 0x30FA)) {
            return false;  // 假名：沒翻成中文（片假名名字要給中文譯名）
        }
    }
    return count <= 12;  // 一句話不是名字
}

std::vector<std::string> findTerms(std::string_view text) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    // 先解成字碼，比對片假名、漢字加敬稱
    std::u32string chars;
    for (std::size_t i = 0; i < text.size();) {
        chars.push_back(nextCodePoint(text, i));
    }
    const auto add = [&](std::u32string_view term) {
        std::string utf8;
        for (const char32_t c : term) {
            appendCodePoint(utf8, c);
        }
        if (seen.insert(utf8).second) {
            out.push_back(std::move(utf8));
        }
    };
    for (std::size_t i = 0; i < chars.size();) {
        if (isKatakana(chars[i])) {
            std::size_t end = i;
            while (end < chars.size() && isKatakana(chars[end])) {
                ++end;
            }
            if (end - i >= 3) {
                add(std::u32string_view(chars).substr(i, end - i));
            }
            i = end;
        } else if (isKanji(chars[i])) {
            std::size_t end = i;
            while (end < chars.size() && isKanji(chars[end])) {
                ++end;
            }
            // 漢字後面接敬稱：「天城さん」。敬稱本身是漢字（君、様…）時，名字是前面那幾個字
            const std::u32string_view rest = std::u32string_view(chars).substr(end);
            const std::u32string_view run = std::u32string_view(chars).substr(i, end - i);
            for (const std::u32string_view honorific : kHonorifics) {
                if (rest.starts_with(honorific) && run.size() <= 4) {
                    add(run);
                    break;
                }
                if (run.size() > honorific.size() && run.ends_with(honorific) &&
                    run.size() - honorific.size() <= 4) {
                    add(run.substr(0, run.size() - honorific.size()));
                    break;
                }
            }
            i = end;
        } else {
            ++i;
        }
    }
    addEnglishTerms(text, out, seen);
    return out;
}

TermMemory::TermMemory(std::filesystem::path file, std::size_t maxPerScope)
    : file_(std::move(file)), maxPerScope_(maxPerScope) {
    load();
}

void TermMemory::load() {
    if (file_.empty()) {
        return;
    }
    std::ifstream in(file_, std::ios::binary);
    if (!in) {
        return;
    }
    const nlohmann::json document = nlohmann::json::parse(in, nullptr, false);
    if (!document.is_object()) {
        return;  // 壞掉的檔案：從頭記起
    }
    for (const auto& [scope, list] : document.items()) {
        if (!list.is_array()) {
            continue;
        }
        for (const auto& entry : list) {
            if (entry.is_array() && entry.size() == 2 && entry[0].is_string() &&
                entry[1].is_string()) {
                const std::string term = entry[0].get<std::string>();
                const std::string translation = entry[1].get<std::string>();
                if (!isUsableTermTranslation(translation)) {
                    continue;  // 以前存進去的壞譯名：丟掉，下次重新翻
                }
                if (terms_[scope].emplace(term, translation).second) {
                    order_[scope].push_back(term);
                }
            }
        }
    }
}

void TermMemory::save() const {
    if (file_.empty()) {
        return;
    }
    nlohmann::json document = nlohmann::json::object();
    for (const auto& [scope, order] : order_) {
        nlohmann::json list = nlohmann::json::array();
        for (const std::string& term : order) {
            list.push_back({term, terms_.at(scope).at(term)});
        }
        document[scope] = std::move(list);
    }
    std::error_code error;
    std::filesystem::create_directories(file_.parent_path(), error);
    const std::filesystem::path temporary = file_.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << document.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
        if (!out.good()) {
            return;
        }
    }
    std::filesystem::rename(temporary, file_, error);
}

Glossary TermMemory::lookup(const std::string& scope, std::span<const std::string> terms) const {
    const std::lock_guard lock(mutex_);
    Glossary out;
    const auto found = terms_.find(scope);
    if (found == terms_.end()) {
        return out;
    }
    for (const std::string& term : terms) {
        if (const auto it = found->second.find(termKey(term)); it != found->second.end()) {
            out.emplace(term, it->second);  // 照這一頁原文的寫法，專有名詞表才對得上
        }
    }
    return out;
}

std::vector<std::string> TermMemory::missing(const std::string& scope,
                                             std::span<const std::string> terms) const {
    const std::lock_guard lock(mutex_);
    std::vector<std::string> out;
    const auto found = terms_.find(scope);
    for (const std::string& term : terms) {
        if (found == terms_.end() || !found->second.contains(termKey(term))) {
            out.push_back(term);
        }
    }
    return out;
}

void TermMemory::remember(const std::string& scope,
                          const std::map<std::string, std::string>& terms) {
    const std::lock_guard lock(mutex_);
    bool changed = false;
    auto& known = terms_[scope];
    auto& order = order_[scope];
    for (const auto& [term, translation] : terms) {
        const std::string key = termKey(term);
        if (key.empty() || !isUsableTermTranslation(translation) ||
            !known.emplace(key, translation).second) {
            continue;
        }
        order.push_back(key);
        changed = true;
    }
    while (order.size() > maxPerScope_) {
        known.erase(order.front());
        order.erase(order.begin());
    }
    if (changed) {
        save();
    }
}

std::size_t TermMemory::size(const std::string& scope) const {
    const std::lock_guard lock(mutex_);
    const auto found = terms_.find(scope);
    return found == terms_.end() ? 0 : found->second.size();
}

}  // namespace tmw::core
