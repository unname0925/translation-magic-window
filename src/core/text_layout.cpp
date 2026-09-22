#include "core/text_layout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <numeric>

#include "core/utf8.h"

namespace tmw::core {
namespace {

// 一個字有多大。
// 橫排看行高就準。直排不能看框的寬度：欄裡只要有「ー」「っ」或標點，框就會被拉窄，
// 同一個對話框的兩欄常常差兩倍以上，合併就被「字級差太多」擋掉。
// 改用「欄長 ÷ 字數」之後，10 頁真實漫畫正確復原的區塊從 86 個增加到 93 個、
// 合併過頭從 7 個降到 4 個（見 docs/design.md 4.4）。
// 垂直於書寫方向的大小：直排是欄寬、橫排是行高。
// 排序判斷「是不是同一欄／同一行」要用它，不能用 fontSize——後者是沿書寫方向算的。
int crossSize(const OcrLine& line) {
    return line.orientation == Orientation::Vertical ? std::max(1, line.rect.width())
                                                     : std::max(1, line.rect.height());
}

// 兩段之間的空隙，重疊時是負的。分群時任意兩行都會互相比較，
// 不能假設誰在前誰在後，所以一律用對稱的算法。
int gapBetween(int a1, int a2, int b1, int b2) {
    return std::max(a1, b1) - std::min(a2, b2);
}

int fontSize(const OcrLine& line) {
    if (line.orientation == Orientation::Horizontal) {
        return std::max(1, line.rect.height());
    }
    const int characters = std::max(1, characterCount(line.text));
    return std::max(1, line.rect.height() / characters);
}

RectI unite(const RectI& a, const RectI& b) {
    if (a.empty()) {
        return b;
    }
    if (b.empty()) {
        return a;
    }
    return {std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right),
            std::max(a.bottom, b.bottom)};
}

// 兩個區間重疊的長度
int overlap(int a0, int a1, int b0, int b1) {
    return std::max(0, std::min(a1, b1) - std::max(a0, b0));
}

// 英文的行尾斷字：「trans-」接「lation」要變成「translation」。
// 只在連字號前後都是字母時才接回去，避免把「well-known」這種原本就有的連字號吃掉。
bool endsWithHyphenatedWord(std::string_view text) {
    if (text.size() < 2 || text.back() != '-') {
        return false;
    }
    const char before = text[text.size() - 2];
    return (before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z');
}

bool startsWithLetter(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    const char first = text.front();
    return (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z');
}

// 兩行能不能算同一段
bool canMerge(const OcrLine& previous, const OcrLine& next, const MergeOptions& options) {
    if (previous.orientation != next.orientation) {
        return false;
    }
    const double size = std::max(1, std::min(fontSize(previous), fontSize(next)));
    const double bigger = std::max(fontSize(previous), fontSize(next));
    if (bigger > size * options.heightRatio) {
        return false;  // 標題和內文不該合併
    }

    if (previous.orientation == Orientation::Horizontal) {
        const int gap =
            gapBetween(previous.rect.top, previous.rect.bottom, next.rect.top, next.rect.bottom);
        if (gap > size * options.lineGapRatio) {
            return false;
        }
        // 左邊對齊、右邊對齊，或其中一行包含另一行（置中的對白）
        const double tolerance = size * options.alignRatio;
        const bool aligned = std::abs(previous.rect.left - next.rect.left) <= tolerance ||
                             std::abs(previous.rect.right - next.rect.right) <= tolerance ||
                             std::abs(previous.rect.center().x - next.rect.center().x) <= tolerance;
        if (!aligned) {
            return false;
        }
        const int shared =
            overlap(previous.rect.left, previous.rect.right, next.rect.left, next.rect.right);
        const int shorter = std::max(1, std::min(previous.rect.width(), next.rect.width()));
        return shared >= shorter * options.overlapRatio;
    }

    // 直排：同一欄被 OCR 切成上下兩塊（左右重疊、上下相接）也是同一段
    const int sameColumnOverlap =
        overlap(previous.rect.left, previous.rect.right, next.rect.left, next.rect.right);
    const int narrower = std::max(1, std::min(previous.rect.width(), next.rect.width()));
    if (sameColumnOverlap >= narrower * options.overlapRatio) {
        const int verticalGap =
            gapBetween(previous.rect.top, previous.rect.bottom, next.rect.top, next.rect.bottom);
        return verticalGap <= size * options.lineGapRatio;
    }

    // 不同欄：兩欄之間的距離（誰在左誰在右都一樣）
    const int gap =
        gapBetween(previous.rect.left, previous.rect.right, next.rect.left, next.rect.right);
    if (gap > size * options.columnGapRatio) {
        return false;
    }
    const double tolerance = size * options.alignRatio;
    const bool aligned = std::abs(previous.rect.top - next.rect.top) <= tolerance ||
                         std::abs(previous.rect.bottom - next.rect.bottom) <= tolerance;
    if (!aligned) {
        return false;
    }
    const int shared =
        overlap(previous.rect.top, previous.rect.bottom, next.rect.top, next.rect.bottom);
    const int shorter = std::max(1, std::min(previous.rect.height(), next.rect.height()));
    return shared >= shorter * options.overlapRatio;
}

}  // namespace

void sortReadingOrder(std::vector<OcrLine>& lines) {
    if (lines.empty()) {
        return;
    }
    // 直排（日文漫畫）：由右到左、同一欄由上到下。
    // 用多數決而不是「每一行都是直排」：漫畫頁面幾乎一定有幾行是橫排（擬聲詞、頁碼、招牌），
    // 只要有一行，整頁的直排欄就會改用「由左到右」排序，整句話的順序就反了。
    const auto verticalCount = std::ranges::count_if(
        lines, [](const OcrLine& line) { return line.orientation == Orientation::Vertical; });
    const bool vertical = verticalCount * 2 > static_cast<std::ptrdiff_t>(lines.size());

    // 先分欄（橫排是分行），再依「第幾欄、欄內由上到下」排序。
    // 不能直接寫成「右緣相差不到一個字就算同一欄」的比較函式：欄寬不一致時會把不同的欄
    // 誤判成同一欄，順序就顛倒了；而且那種比較不具備遞移性，std::sort 要求嚴格弱序。
    std::vector<std::size_t> order(lines.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    // 掃描的方向：直排由右到左，橫排由上到下
    std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) {
        return vertical ? lines[a].rect.right > lines[b].rect.right
                        : lines[a].rect.top < lines[b].rect.top;
    });

    std::vector<int> group(lines.size(), 0);
    int current = 0;
    int currentLow = vertical ? lines[order.front()].rect.left : lines[order.front()].rect.top;
    int currentHigh = vertical ? lines[order.front()].rect.right : lines[order.front()].rect.bottom;
    for (const std::size_t index : order) {
        const RectI& rect = lines[index].rect;
        const int low = vertical ? rect.left : rect.top;
        const int high = vertical ? rect.right : rect.bottom;
        const int shared = overlap(currentLow, currentHigh, low, high);
        const int narrower = std::max(1, std::min(currentHigh - currentLow, high - low));
        if (shared * 2 >= narrower) {
            // 和目前這一欄重疊超過一半：同一欄，範圍跟著擴大
            currentLow = std::min(currentLow, low);
            currentHigh = std::max(currentHigh, high);
        } else {
            ++current;
            currentLow = low;
            currentHigh = high;
        }
        group[index] = current;
    }

    // 排索引再重建：直接排 lines 的話，比較函式拿不到「這個元素原本是第幾個」——
    // 排序過程中元素會被搬動，用位址算出來的索引馬上就失效了。
    std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) {
        if (group[a] != group[b]) {
            return group[a] < group[b];
        }
        return vertical ? lines[a].rect.top < lines[b].rect.top
                        : lines[a].rect.left < lines[b].rect.left;
    });
    std::vector<OcrLine> sorted;
    sorted.reserve(lines.size());
    for (const std::size_t index : order) {
        sorted.push_back(std::move(lines[index]));
    }
    lines = std::move(sorted);
}

std::string joinLines(std::span<const std::string> lines, Language language,
                      std::vector<int>* startOffsets) {
    std::string result;
    if (startOffsets != nullptr) {
        startOffsets->clear();
        startOffsets->reserve(lines.size());
    }
    const auto record = [&] {
        if (startOffsets != nullptr) {
            startOffsets->push_back(characterCount(result));
        }
    };
    for (const std::string& line : lines) {
        if (result.empty()) {
            record();
            result = line;
            continue;
        }
        if (language == Language::Japanese) {
            record();
            result += line;  // 日文直接相連
            continue;
        }
        if (language == Language::English && endsWithHyphenatedWord(result) &&
            startsWithLetter(line)) {
            result.pop_back();  // 去掉行尾的連字號，把被斷開的單字接回去
            record();
            result += line;
            continue;
        }
        result += ' ';
        record();
        result += line;
    }
    return result;
}

void resolveAmbiguousOrientation(std::vector<OcrLine>& lines) {
    // 長寬比在這個範圍內就算「看不出方向」
    constexpr double kAmbiguous = 1.5;
    int vertical = 0;
    int horizontal = 0;
    for (const OcrLine& line : lines) {
        const int width = std::max(1, line.rect.width());
        const int height = std::max(1, line.rect.height());
        if (height > width * kAmbiguous) {
            ++vertical;
        } else if (width > height * kAmbiguous) {
            ++horizontal;
        }
    }
    if (vertical == horizontal) {
        return;  // 沒有多數可以參考，維持原樣
    }
    const Orientation majority =
        vertical > horizontal ? Orientation::Vertical : Orientation::Horizontal;
    for (OcrLine& line : lines) {
        const int width = std::max(1, line.rect.width());
        const int height = std::max(1, line.rect.height());
        if (height <= width * kAmbiguous && width <= height * kAmbiguous) {
            line.orientation = majority;
        }
    }
}

std::vector<TextBlock> mergeIntoBlocks(std::span<const OcrLine> lines,
                                       const MergeOptions& options) {
    std::vector<OcrLine> sorted(lines.begin(), lines.end());
    std::erase_if(sorted, [](const OcrLine& line) { return line.text.empty(); });
    resolveAmbiguousOrientation(sorted);
    sortReadingOrder(sorted);

    // 任意兩行只要相容就併成同一群，不是只看閱讀順序上相鄰的那兩行。
    // 被 ルビ 或擬聲詞插隊一次，後面整串就接不回來（design.md 4.4 的實測）。
    const std::size_t count = sorted.size();
    std::vector<std::size_t> parent(count);
    for (std::size_t i = 0; i < count; ++i) {
        parent[i] = i;
    }
    const std::function<std::size_t(std::size_t)> find = [&](std::size_t i) {
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        return i;
    };
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1; j < count; ++j) {
            if (canMerge(sorted[i], sorted[j], options)) {
                parent[find(i)] = find(j);
            }
        }
    }

    // 每一群的位置由它第一行的閱讀順序決定
    std::vector<TextBlock> blocks;
    std::vector<std::size_t> blockOf(count, count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t root = find(i);
        if (blockOf[root] == count) {
            blockOf[root] = blocks.size();
            TextBlock block;
            block.rect = sorted[i].rect;
            block.orientation = sorted[i].orientation;
            blocks.push_back(std::move(block));
        }
        TextBlock& block = blocks[blockOf[root]];
        block.rect = unite(block.rect, sorted[i].rect);
        block.lines.push_back(std::move(sorted[i]));
    }

    for (TextBlock& block : blocks) {
        std::vector<std::string> texts;
        texts.reserve(block.lines.size());
        double score = 0.0;
        for (const OcrLine& line : block.lines) {
            texts.push_back(line.text);
            score += line.score;
        }
        // 先用全部的文字判斷語言，再依語言決定怎麼接（日文不加空格）
        std::string joined;
        for (const std::string& text : texts) {
            joined += text;
        }
        block.language = detectLanguage(joined);
        std::vector<int> offsets;
        block.text = joinLines(texts, block.language, &offsets);
        // ルビ 的位置是「在那一行中的第幾個字」，接成整段之後要跟著往後移
        for (std::size_t i = 0; i < block.lines.size() && i < offsets.size(); ++i) {
            for (const RubyAnnotation& one : block.lines[i].ruby) {
                block.ruby.push_back(
                    RubyAnnotation{one.start + offsets[i], one.length, one.reading});
            }
        }
        std::sort(
            block.ruby.begin(), block.ruby.end(),
            [](const RubyAnnotation& a, const RubyAnnotation& b) { return a.start < b.start; });
        block.score = static_cast<float>(score / static_cast<double>(block.lines.size()));
    }
    return blocks;
}

bool touchesEdge(const RectI& rect, const SizeI& frame, int margin) {
    return rect.left <= margin || rect.top <= margin || rect.right >= frame.width - margin ||
           rect.bottom >= frame.height - margin;
}

std::vector<TextBlock> dropEdgeBlocks(std::span<const TextBlock> blocks, const SizeI& frame,
                                      int margin) {
    std::vector<TextBlock> kept;
    kept.reserve(blocks.size());
    for (const TextBlock& block : blocks) {
        if (!touchesEdge(block.rect, frame, margin)) {
            kept.push_back(block);
        }
    }
    return kept;
}

}  // namespace tmw::core
