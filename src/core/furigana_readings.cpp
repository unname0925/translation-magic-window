#include "core/furigana_readings.h"

#include <cstddef>
#include <functional>
#include <vector>

#include "core/utf8.h"

namespace tmw::core {
namespace {

void appendUtf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

bool isKanji(char32_t c) {
    return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) || c == U'々';
}

std::u32string decode(std::string_view text) {
    std::u32string out;
    for (std::size_t i = 0; i < text.size();) {
        out += nextCodePoint(text, i);
    }
    return out;
}

std::string encode(char32_t c) {
    std::string out;
    appendUtf8(out, c);
    return out;
}

char32_t hiragana(char32_t c) {
    return c >= U'ァ' && c <= U'ヶ' ? c - 0x60 : c;
}

// 連濁（か→が、は→ば／ぱ）
std::u32string voiced(char32_t first) {
    static const std::u32string from = U"かきくけこさしすせそたちつてとはひふへほ";
    static const std::u32string to = U"がぎぐげござじずぜぞだぢづでどばびぶべぼ";
    std::u32string out;
    if (const std::size_t at = from.find(first); at != std::u32string::npos) {
        out += to[at];
    }
    static const std::u32string half = U"はひふへほ";
    static const std::u32string halfTo = U"ぱぴぷぺぽ";
    if (const std::size_t at = half.find(first); at != std::u32string::npos) {
        out += halfTo[at];
    }
    return out;
}

// 一個漢字的讀音，加上連濁和促音的變化（和 tools/eval/furigana_dict.py 的 variants 相同）
std::vector<std::u32string> variants(const std::u32string& reading) {
    std::vector<std::u32string> out{reading};
    if (!reading.empty()) {
        for (const char32_t c : voiced(reading.front())) {
            out.push_back(c + reading.substr(1));
        }
    }
    const std::size_t base = out.size();
    for (std::size_t i = 0; i < base; ++i) {
        const std::u32string& word = out[i];
        if (word.size() >= 2 &&
            std::u32string(U"つくちき").find(word.back()) != std::u32string::npos) {
            out.push_back(word.substr(0, word.size() - 1) + U"っ");
        }
    }
    return out;
}

}  // namespace

std::string toHiragana(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        appendUtf8(out, hiragana(nextCodePoint(text, i)));
    }
    return out;
}

std::optional<FuriganaReadings> FuriganaReadings::parse(std::string_view text) {
    FuriganaReadings out;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.size() < 3 || line[1] != ' ' || (line[0] != 'K' && line[0] != 'W')) {
            continue;  // 註解或看不懂的行
        }
        const std::size_t tab = line.find('\t');
        if (tab == std::string_view::npos) {
            continue;
        }
        const std::string word(line.substr(2, tab - 2));
        std::string_view readings = line.substr(tab + 1);
        if (line[0] == 'W') {
            out.irregular_.emplace(word, std::string(readings));
            continue;
        }
        std::set<std::string>& set = out.kanji_[word];
        while (!readings.empty()) {
            const std::size_t comma = readings.find(',');
            if (const std::string_view one = readings.substr(0, comma); !one.empty()) {
                set.emplace(one);
            }
            readings.remove_prefix(comma == std::string_view::npos ? readings.size() : comma + 1);
        }
    }
    if (out.kanji_.empty()) {
        return std::nullopt;
    }
    return out;
}

std::optional<bool> FuriganaReadings::isSpecial(std::string_view base,
                                                std::string_view reading) const {
    const std::string normalized = toHiragana(reading);
    if (irregular_.contains(std::make_pair(std::string(base), normalized))) {
        return false;
    }
    const std::u32string word = decode(base);
    const std::u32string target = decode(normalized);

    // 每個漢字可能的讀音（含連濁、促音）。有不認得的漢字就判斷不了。
    std::vector<std::vector<std::u32string>> options(word.size());
    for (std::size_t i = 0; i < word.size(); ++i) {
        const char32_t c = word[i];
        if (!isKanji(c)) {
            options[i] = {std::u32string(1, hiragana(c))};  // 夾在中間的假名照原樣比對
            continue;
        }
        // 「々」重複前一個字（也可能連濁：人々＝ひとびと）
        const char32_t source = c == U'々' && i > 0 ? word[i - 1] : c;
        const auto found = kanji_.find(encode(source));
        if (found == kanji_.end()) {
            return std::nullopt;
        }
        for (const std::string& one : found->second) {
            for (std::u32string& form : variants(decode(one))) {
                options[i].push_back(std::move(form));
            }
        }
    }

    // 動態規劃：reachable[i][j] 代表前 i 個字剛好拼出讀音的前 j 個假名
    std::vector<std::vector<bool>> reachable(word.size() + 1,
                                             std::vector<bool>(target.size() + 1, false));
    reachable[0][0] = true;
    for (std::size_t i = 0; i < word.size(); ++i) {
        for (std::size_t j = 0; j <= target.size(); ++j) {
            if (!reachable[i][j]) {
                continue;
            }
            for (const std::u32string& form : options[i]) {
                if (!form.empty() && target.compare(j, form.size(), form) == 0) {
                    reachable[i + 1][j + form.size()] = true;
                }
            }
        }
    }
    return !reachable[word.size()][target.size()];
}

}  // namespace tmw::core
