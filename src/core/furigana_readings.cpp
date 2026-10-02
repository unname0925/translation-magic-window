#include "core/furigana_readings.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string_view>
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

bool isKana(char32_t c) {
    return (c >= U'ぁ' && c <= U'ゖ') || (c >= U'ァ' && c <= U'ヺ') || c == U'ー' || c == U'・';
}

bool isKatakana(char32_t c) {
    return (c >= U'ァ' && c <= U'ヺ') || c == U'ー';
}

bool isLatin(char32_t c) {
    return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= U'Ａ' && c <= U'Ｚ') ||
           (c >= U'ａ' && c <= U'ｚ');
}

// 不是作者寫的讀音，是 OCR 讀錯或配錯的：讀音裡有數字、符號、漢字（振り仮名只會是假名，
// 偶爾是英文字母），或本文一個字都沒有（ルビ配到了「！」上）
bool ocrNoise(const std::u32string& base, const std::u32string& reading) {
    const bool hasWord = std::any_of(
        base.begin(), base.end(), [](char32_t c) { return isKanji(c) || isKana(c) || isLatin(c); });
    return !hasWord || std::any_of(reading.begin(), reading.end(),
                                   [](char32_t c) { return !isKana(c) && !isLatin(c); });
}

std::u32string decode(std::string_view text) {
    std::u32string out;
    for (std::size_t i = 0; i < text.size();) {
        out += nextCodePoint(text, i);
    }
    return out;
}

std::string encode(std::u32string_view text) {
    std::string out;
    for (const char32_t c : text) {
        appendUtf8(out, c);
    }
    return out;
}

char32_t hiragana(char32_t c) {
    return c >= U'ァ' && c <= U'ヶ' ? c - 0x60 : c;
}

// OCR 最常讀錯振り仮名的地方抹平：濁點、半濁點拿掉，小字當大字（がっこう → かつこう）
std::u32string loosen(std::u32string_view text) {
    static const std::u32string small = U"ぁぃぅぇぉっゃゅょゎゕゖ";
    static const std::u32string large = U"あいうえおつやゆよわかけ";
    static const std::u32string voicedKana = U"がぎぐげござじずぜぞだぢづでどばびぶべぼぱぴぷぺぽ";
    static const std::u32string plainKana = U"かきくけこさしすせそたちつてとはひふへほはひふへほ";
    std::u32string out;
    out.reserve(text.size());
    for (char32_t c : text) {
        c = hiragana(c);
        if (const std::size_t at = small.find(c); at != std::u32string::npos) {
            c = large[at];
        }
        if (const std::size_t at = voicedKana.find(c); at != std::u32string::npos) {
            c = plainKana[at];
        }
        out += c;
    }
    return out;
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

// rest 的開頭和 form 只差一個假名（換了一個、少了一個、多了一個）時，rest 要用掉幾個字
// （和 furigana_dict.py 的 one_edit_lengths 相同）
std::vector<std::size_t> oneEditLengths(std::u32string_view form, std::u32string_view rest) {
    const std::size_t n = form.size();
    std::vector<std::size_t> out;
    if (rest.size() >= n) {
        std::size_t differences = 0;
        for (std::size_t k = 0; k < n; ++k) {
            differences += rest[k] != form[k] ? 1 : 0;
        }
        if (differences == 1) {
            out.push_back(n);
        }
    }
    // 少了一個字：form 拿掉第 k 個，等於 rest 的開頭
    if (n >= 2 && rest.size() >= n - 1) {
        for (std::size_t k = 0; k < n; ++k) {
            if (form.substr(0, k) == rest.substr(0, k) &&
                form.substr(k + 1) == rest.substr(k, n - 1 - k)) {
                out.push_back(n - 1);
                break;
            }
        }
    }
    // 多了一個字：rest 的開頭拿掉第 k 個，等於 form
    if (rest.size() >= n + 1) {
        for (std::size_t k = 0; k <= n; ++k) {
            if (rest.substr(0, k) == form.substr(0, k) &&
                rest.substr(k + 1, n - k) == form.substr(k)) {
                out.push_back(n + 1);
                break;
            }
        }
    }
    return out;
}

// 前 i 個字能不能剛好拼出讀音，最多容許 edits 個錯字（動態規劃）。
// options[i] 是第 i 個字可能的讀音，和 target 用同一種方式正規化過。
bool derivable(const std::vector<std::vector<std::u32string>>& options,
               const std::u32string& target, int edits) {
    const std::size_t words = options.size();
    const std::size_t budgets = static_cast<std::size_t>(edits) + 1;
    // reachable[i][j][e]：前 i 個字拼出讀音的前 j 個假名，用了 e 個錯字
    std::vector<std::vector<std::vector<bool>>> reachable(
        words + 1, std::vector<std::vector<bool>>(target.size() + 1, std::vector<bool>(budgets)));
    reachable[0][0][0] = true;
    const std::u32string_view whole(target);
    for (std::size_t i = 0; i < words; ++i) {
        for (std::size_t j = 0; j <= target.size(); ++j) {
            for (std::size_t e = 0; e < budgets; ++e) {
                if (!reachable[i][j][e]) {
                    continue;
                }
                for (const std::u32string& form : options[i]) {
                    if (form.empty()) {
                        continue;
                    }
                    if (target.compare(j, form.size(), form) == 0) {
                        reachable[i + 1][j + form.size()][e] = true;
                    }
                    if (e + 1 < budgets) {
                        for (const std::size_t used : oneEditLengths(form, whole.substr(j))) {
                            reachable[i + 1][j + used][e + 1] = true;
                        }
                    }
                }
            }
        }
    }
    return std::any_of(reachable[words][target.size()].begin(),
                       reachable[words][target.size()].end(), [](bool yes) { return yes; });
}

// 讀音容許錯一個假名的長度下限：兩個字的讀音錯一個就面目全非了
// （時間＝とき 不能因為像「じき」就算一般讀音）
constexpr std::size_t kMinLengthForEdits = 3;

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
            out.looseIrregular_.emplace(word, encode(loosen(decode(readings))));
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
    const std::u32string word = decode(base);
    const std::u32string spoken = decode(reading);
    if (ocrNoise(word, spoken)) {
        return false;
    }
    const std::string normalized = toHiragana(reading);
    if (irregular_.contains(std::make_pair(std::string(base), normalized))) {
        return false;
    }

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
        const auto found = kanji_.find(encode(std::u32string(1, source)));
        if (found == kanji_.end()) {
            return std::nullopt;
        }
        for (const std::string& one : found->second) {
            for (std::u32string& form : variants(decode(one))) {
                options[i].push_back(std::move(form));
            }
        }
    }
    if (derivable(options, decode(normalized), 0)) {
        return false;
    }

    // 以下放寬給 OCR 讀錯的平假名。片假名是作者刻意的強烈訊號（楓男＝フーダン）。
    if (std::any_of(spoken.begin(), spoken.end(), isKatakana)) {
        return true;
    }
    const std::u32string loose = loosen(spoken);
    if (looseIrregular_.contains(std::make_pair(std::string(base), encode(loose)))) {
        return false;
    }
    for (std::vector<std::u32string>& forms : options) {
        for (std::u32string& form : forms) {
            form = loosen(form);
        }
    }
    const int edits = spoken.size() >= kMinLengthForEdits ? 1 : 0;
    return !derivable(options, loose, edits);
}

}  // namespace tmw::core
