#include "core/ruby.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
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
        if (small.text.empty() || !isKanaOnly(small.text)) {
            continue;
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
            if (smallSize > baseSize * options.maxSizeRatio) {
                continue;  // 不夠小，是一般的文字行
            }
            const auto [baseStart, baseEnd] = along(base);
            const int shared = overlap(smallStart, smallEnd, baseStart, baseEnd);
            if (shared < (smallEnd - smallStart) * options.minCoverRatio) {
                continue;  // 沒有落在本文的範圍內
            }
            if (shared > (baseEnd - baseStart) * options.maxSpanRatio) {
                continue;  // 蓋住整欄，那是另一句話
            }
            const auto [baseNear, baseFar] = across(base);
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

std::string stripRubyMarkup(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t open = text.find('{', at);
        if (open == std::string_view::npos) {
            break;
        }
        const std::size_t bar = text.find('|', open + 1);
        const std::size_t close = text.find('}', open + 1);
        const std::size_t nextOpen = text.find('{', open + 1);
        if (bar == std::string_view::npos || close == std::string_view::npos || bar > close ||
            (nextOpen != std::string_view::npos && nextOpen < bar)) {
            out.append(text.substr(at, open - at + 1));  // 沒有配對的大括號照原樣留著
            at = open + 1;
            continue;
        }
        out.append(text.substr(at, open - at));
        out.append(text.substr(open + 1, bar - open - 1));  // 只留本文
        at = close + 1;
    }
    out.append(text.substr(at));
    return out;
}

}  // namespace tmw::core
