#include "core/ruby.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

#include "core/language.h"
#include "core/utf8.h"

namespace tmw::core {
namespace {

// 直排時字的大小看寬度，橫排看高度
int fontSize(const OcrLine& line) {
    return line.orientation == Orientation::Vertical ? line.rect.width() : line.rect.height();
}

// 沿著「寫字的方向」的起點和終點
std::pair<int, int> along(const OcrLine& line) {
    return line.orientation == Orientation::Vertical ? std::pair{line.rect.top, line.rect.bottom}
                                                     : std::pair{line.rect.left, line.rect.right};
}

// 垂直於寫字方向的那一軸
std::pair<int, int> across(const OcrLine& line) {
    return line.orientation == Orientation::Vertical ? std::pair{line.rect.left, line.rect.right}
                                                     : std::pair{line.rect.top, line.rect.bottom};
}

int overlap(int a1, int a2, int b1, int b2) {
    return std::max(0, std::min(a2, b2) - std::max(a1, b1));
}

// 兩段之間的空隙。重疊時是負的。
int gapBetween(int a1, int a2, int b1, int b2) {
    return std::max(a1, b1) - std::min(a2, b2);
}

}  // namespace

bool isKanaOnly(std::string_view utf8) {
    const ScriptCounts counts = countScripts(utf8);
    return counts.kana > 0 && counts.han == 0 && counts.latin == 0 && counts.hangul == 0;
}

RubyResult attachRuby(std::span<const OcrLine> lines, const RubyOptions& options) {
    RubyResult result;
    std::vector<bool> isRuby(lines.size(), false);
    // 每一行本文收到的 ルビ
    std::vector<std::vector<RubyAnnotation>> attached(lines.size());

    for (std::size_t candidate = 0; candidate < lines.size(); ++candidate) {
        const OcrLine& small = lines[candidate];
        if (small.text.empty()) {
            continue;
        }
        // 位置像 ルビ、字卻不是假名的：可能是 OCR 把太小的假名讀錯了（實測會讀成「11.5」
        // 「2104」這種數字），也可能就是一般的文字。只丟前者：
        // - 裡面有假名或漢字的一律當成一般文字。「欄長 ÷ 字數」估出來的字級在字數少、字距
        //   拉很開的鄰欄旁邊會失準，只看位置的話，「みにゃ…皆も」這種真正的對白也會被丟掉。
        // - 比寬度而不是字級：讀錯的文字，字數本身就沒有意義，「欄長 ÷ 字數」算不出東西。
        const bool kana = isKanaOnly(small.text);
        if (!kana) {
            const ScriptCounts scripts = countScripts(small.text);
            if (scripts.kana + scripts.han > 0) {
                continue;
            }
        }
        const int smallSize = std::max(1, fontSize(small));
        const auto [smallStart, smallEnd] = along(small);
        const auto [smallNear, smallFar] = across(small);

        std::size_t best = lines.size();
        int bestGap = 0;
        for (std::size_t other = 0; other < lines.size(); ++other) {
            if (other == candidate || lines[other].text.empty()) {
                continue;
            }
            const OcrLine& base = lines[other];
            if (base.orientation != small.orientation) {
                continue;
            }
            const int baseSize = std::max(1, fontSize(base));
            const auto [baseNear, baseFar] = across(base);
            if (kana) {
                if (smallSize > baseSize * options.maxSizeRatio) {
                    continue;  // 不夠小，是一般的文字行
                }
            } else if ((smallFar - smallNear) >
                       (baseFar - baseNear) * options.maxMisreadWidthRatio) {
                continue;  // 不夠細，是一般的文字（「Apollo」「10秒前」這種）
            }
            const auto [baseStart, baseEnd] = along(base);
            const int shared = overlap(smallStart, smallEnd, baseStart, baseEnd);
            if (shared < (smallEnd - smallStart) * options.minCoverRatio) {
                continue;  // 沒有落在本文的範圍內
            }
            if (shared > (baseEnd - baseStart) * options.maxSpanRatio) {
                continue;  // 蓋住整欄，那是另一句話
            }
            const int gap = gapBetween(smallNear, smallFar, baseNear, baseFar);
            if (gap > baseSize * options.maxGapRatio) {
                continue;  // 隔壁那一欄
            }
            if (best == lines.size() || gap < bestGap) {
                best = other;
                bestGap = gap;
            }
        }
        if (best == lines.size()) {
            continue;
        }
        if (!kana) {
            isRuby[candidate] = true;
            ++result.dropped;
            continue;
        }

        // ルビ 在本文那一欄上的位置，換算成「第幾個字到第幾個字」
        const OcrLine& base = lines[best];
        const auto [baseStart, baseEnd] = along(base);
        const int baseLength = std::max(1, baseEnd - baseStart);
        const int characters = characterCount(base.text);
        const auto toIndex = [&](int position) {
            const double ratio = static_cast<double>(position - baseStart) / baseLength;
            return std::clamp(static_cast<int>(std::lround(ratio * characters)), 0, characters);
        };
        int start = toIndex(smallStart);
        int end = toIndex(smallEnd);
        if (end <= start) {
            end = std::min(start + 1, characters);
        }
        if (end <= start) {
            continue;  // 本文是空的
        }
        isRuby[candidate] = true;
        attached[best].push_back(RubyAnnotation{start, end - start, small.text});
        ++result.attached;
    }

    result.lines.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (isRuby[i]) {
            continue;
        }
        OcrLine line = lines[i];
        line.ruby = std::move(attached[i]);
        std::sort(
            line.ruby.begin(), line.ruby.end(),
            [](const RubyAnnotation& a, const RubyAnnotation& b) { return a.start < b.start; });
        result.lines.push_back(std::move(line));
    }
    return result;
}

bool isSpecialReading(std::string_view reading) {
    if (reading.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < reading.size();) {
        const char32_t c = nextCodePoint(reading, i);
        // 片假名（含長音符號 ー 和中點 ・），以及半形片假名
        const bool katakana = (c >= 0x30A0 && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF) ||
                              (c >= 0xFF66 && c <= 0xFF9F);
        if (!katakana) {
            return false;
        }
    }
    return true;
}

std::string markSpecialRuby(std::string_view text, std::span<const RubyAnnotation> ruby) {
    std::vector<RubyAnnotation> special;
    for (const RubyAnnotation& one : ruby) {
        if (isSpecialReading(one.reading)) {
            special.push_back(one);
        }
    }
    return markRuby(text, special);
}

std::string markRuby(std::string_view text, std::span<const RubyAnnotation> ruby) {
    if (ruby.empty()) {
        return std::string(text);
    }
    std::string out;
    out.reserve(text.size() + ruby.size() * 8);
    int taken = 0;  // 已經處理到第幾個字
    for (const RubyAnnotation& one : ruby) {
        if (one.length <= 0 || one.start < taken) {
            continue;  // 重疊的略過，避免產生壞掉的標記
        }
        const std::size_t from = byteOffsetOfCharacter(text, taken);
        const std::size_t base = byteOffsetOfCharacter(text, one.start);
        const std::size_t to = byteOffsetOfCharacter(text, one.start + one.length);
        if (base >= text.size()) {
            break;
        }
        out.append(text.substr(from, base - from));
        out += '{';
        out.append(text.substr(base, to - base));
        out += '|';
        out += one.reading;
        out += '}';
        taken = one.start + one.length;
    }
    out.append(text.substr(byteOffsetOfCharacter(text, taken)));
    return out;
}

namespace {

// 從 at 開始找下一個 `{本文|讀音}`。沒有配對的大括號（`{` 後面沒有 `|` 和 `}`）不算。
struct Markup {
    std::size_t open = 0;
    std::size_t bar = 0;
    std::size_t close = 0;
};
std::optional<Markup> findMarkup(std::string_view text, std::size_t at) {
    while (at < text.size()) {
        const std::size_t open = text.find('{', at);
        if (open == std::string_view::npos) {
            return std::nullopt;
        }
        const std::size_t bar = text.find('|', open + 1);
        const std::size_t close = text.find('}', open + 1);
        const std::size_t nextOpen = text.find('{', open + 1);
        if (bar == std::string_view::npos || close == std::string_view::npos || bar > close ||
            (nextOpen != std::string_view::npos && nextOpen < bar)) {
            at = open + 1;  // 這個大括號不是標記，從下一個字繼續找
            continue;
        }
        return Markup{open, bar, close};
    }
    return std::nullopt;
}

}  // namespace

std::string stripRubyMarkup(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    std::size_t at = 0;
    while (const std::optional<Markup> found = findMarkup(text, at)) {
        out.append(text.substr(at, found->open - at));  // 沒有配對的大括號照原樣留著
        out.append(text.substr(found->open + 1, found->bar - found->open - 1));  // 只留本文
        at = found->close + 1;
    }
    out.append(text.substr(at));
    return out;
}

std::vector<std::pair<std::string, std::string>> rubyMarkupPairs(std::string_view text) {
    std::vector<std::pair<std::string, std::string>> pairs;
    std::size_t at = 0;
    while (const std::optional<Markup> found = findMarkup(text, at)) {
        pairs.emplace_back(text.substr(found->open + 1, found->bar - found->open - 1),
                           text.substr(found->bar + 1, found->close - found->bar - 1));
        at = found->close + 1;
    }
    return pairs;
}

std::vector<RubyAnnotation> remapRuby(std::string_view oldText,
                                      std::span<const RubyAnnotation> ruby,
                                      std::string_view newText) {
    // 以「字」為單位比對：位置的單位也是字
    const auto toCharacters = [](std::string_view text) {
        std::u32string out;
        for (std::size_t index = 0; index < text.size();) {
            out += nextCodePoint(text, index);
        }
        return out;
    };
    const std::u32string before = toCharacters(oldText);
    const std::u32string after = toCharacters(newText);

    std::vector<RubyAnnotation> remapped;
    std::size_t cursor = 0;
    for (const RubyAnnotation& one : ruby) {
        if (one.start < 0 || one.length <= 0 ||
            static_cast<std::size_t>(one.start + one.length) > before.size()) {
            continue;
        }
        const std::u32string base = before.substr(static_cast<std::size_t>(one.start),
                                                  static_cast<std::size_t>(one.length));
        const std::size_t found = after.find(base, cursor);
        if (found == std::u32string::npos) {
            continue;  // 新的版本把這幾個字讀成別的了
        }
        remapped.push_back(RubyAnnotation{static_cast<int>(found), one.length, one.reading});
        cursor = found + base.size();
    }
    return remapped;
}

}  // namespace tmw::core
