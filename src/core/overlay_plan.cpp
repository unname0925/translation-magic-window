#include "core/overlay_plan.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

#include "core/language.h"
#include "core/ruby.h"
#include "core/text_style.h"
#include "core/utf8.h"

namespace tmw::core {
namespace {

// 原文框往外多蓋這麼多像素：OCR 的框常常貼著字，反鋸齒的邊緣會露出來
constexpr int kPadding = 3;
// 哪些段落要蓋。用純色蓋在畫上會是一塊突兀的色塊，而且擬聲詞本來就不該蓋（design.md 4.8），
// 這些留著原文，譯文在結果視窗裡。3 頁日文漫畫、3 張日文遊戲截圖上量的：
// - 外圍至少 80% 和背景色差不到 24：漫畫對話框是 0.85～1.00，擬聲詞和畫在圖上的字 0.03～0.76
constexpr double kMinUniformity = 0.8;
// - 遊戲的對話框是半透明的，透出底下的畫面，只有 0.72～0.84；放寬到差 72 以內是 0.90～0.95。
//   擬聲詞放寬後也有到 0.86～0.90 的，但 OCR 分數都在 0.6 以下（對話是 0.82 以上），
//   所以放寬的這條要 OCR 很有把握才算
constexpr int kLooseTolerance = 72;
constexpr double kMinLooseUniformity = 0.9;
constexpr float kMinScoreForLoose = 0.9f;
// - OCR 分數這麼低的多半是把圖示、花紋讀成了字（遊戲的按鈕讀成「迴」），不蓋
constexpr float kMinScore = 0.5f;
// 對話框偵測器框到的文字（TextBlock::inBubble）：花俏的字體、ルビ 常讓 PP-OCR 的分數偏低，
// 文字也已經交給 manga-ocr 重讀。網頁漫畫 34 頁實測，框到的段落分數最低 0.36
constexpr float kMinScoreInBubble = 0.3f;
// 背景修補（M4-01）只給至少這麼多個字的段落：「！」「2-」這種不是要翻的文字
constexpr int kMinLettersForInpainting = 2;
// 背景不是純色、對話框偵測器也沒框到的段落（手寫的碎碎念、標籤），至少要這麼有把握才修補。
// 以前和 kMinScoreForLoose 一樣是 0.9，網頁漫畫 34 頁裡分數 0.6～0.9 的手寫對白都沒蓋；
// 把符號讀成字的小框另外由 looksLikeSymbols 擋掉
constexpr float kMinScoreForInpainting = 0.6f;
// 原文沒有字，或不超過這麼多個字、框又小於畫面的這個比例：多半是把愛心、閃光這類小符號
// 讀成了字。網頁漫畫 34 頁實測，蓋錯的 16 個框裡 14 個是這種；真正的短對白框都比較大
constexpr int kMaxLettersForSymbols = 3;
constexpr double kMaxSymbolAreaRatio = 0.0015;
// 譯文和補出來的背景至少要有這麼多亮度對比（WCAG 對大字的要求）
constexpr double kMinPatchContrast = 3.0;

// 英文字母、假名、漢字、韓文（標點、數字、符號不算）
bool isLetterLike(char32_t c) {
    return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= 0x3041 && c <= 0x30FA) ||
           (c >= 0x3400 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) ||
           (c >= 0xFF21 && c <= 0xFF3A) || (c >= 0xFF41 && c <= 0xFF5A);
}

int letterCount(const std::string& text) {
    int letters = 0;
    for (std::size_t i = 0; i < text.size();) {
        letters += isLetterLike(nextCodePoint(text, i)) ? 1 : 0;
    }
    return letters;
}

bool looksLikeSymbols(const ImageBgra& frame, const TextBlock& block) {
    const int letters = letterCount(block.text);
    const double area = static_cast<double>(block.rect.width()) * block.rect.height();
    const double frameArea = static_cast<double>(frame.width) * frame.height;
    return letters == 0 ||
           (letters <= kMaxLettersForSymbols && area < kMaxSymbolAreaRatio * frameArea);
}

Rgba averageColor(const ImageBgra& image) {
    std::uint64_t r = 0;
    std::uint64_t g = 0;
    std::uint64_t b = 0;
    const std::uint64_t count = static_cast<std::uint64_t>(image.width) * image.height;
    for (std::size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
        b += image.pixels[i];
        g += image.pixels[i + 1];
        r += image.pixels[i + 2];
    }
    if (count == 0) {
        return Rgba{255, 255, 255, 255};
    }
    return Rgba{static_cast<std::uint8_t>(r / count), static_cast<std::uint8_t>(g / count),
                static_cast<std::uint8_t>(b / count), 255};
}

// net/ruby_notes 接在譯文後面的註解：「　［本気（マジ）→ 認真（玩真的）、…］」
constexpr std::string_view kNotesOpen = "　［";
constexpr std::string_view kNotesClose = "］";

// 註解裡每一條「→」右邊的（譯文的詞, 讀音的譯文）。看不懂的部分略過。
std::vector<std::pair<std::string, std::string>> parseRubyNotes(std::string_view notes) {
    constexpr std::string_view kArrow = "）→ ";
    constexpr std::string_view kOpen = "（";
    constexpr std::string_view kEnd = "）、";
    constexpr std::string_view kClose = "）";
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t at = 0;
    while (true) {
        const std::size_t arrow = notes.find(kArrow, at);
        if (arrow == std::string_view::npos) {
            break;
        }
        const std::size_t word = arrow + kArrow.size();
        const std::size_t open = notes.find(kOpen, word);
        if (open == std::string_view::npos) {
            break;
        }
        // 讀音的譯文到「）、」（下一條）或最後一個「）」為止
        std::size_t close = notes.find(kEnd, open);
        const bool last = close == std::string_view::npos;
        if (last) {
            close = notes.ends_with(kClose) ? notes.size() - kClose.size() : notes.size();
        }
        const std::string_view base = notes.substr(word, open - word);
        const std::string_view reading =
            notes.substr(open + kOpen.size(), close - open - kOpen.size());
        if (!base.empty() && !reading.empty()) {
            out.emplace_back(base, reading);
        }
        if (last) {
            break;
        }
        at = close + kEnd.size();
    }
    return out;
}

std::uint8_t median(std::vector<std::uint8_t>& values) {
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

int lineThickness(const TextBlock& block) {
    std::vector<int> sizes;
    sizes.reserve(block.lines.size());
    for (const OcrLine& line : block.lines) {
        sizes.push_back(line.orientation == Orientation::Vertical ? line.rect.width()
                                                                  : line.rect.height());
    }
    if (sizes.empty()) {
        return 0;
    }
    const auto middle = sizes.begin() + static_cast<std::ptrdiff_t>(sizes.size() / 2);
    std::nth_element(sizes.begin(), middle, sizes.end());
    return *middle;
}

}  // namespace

Rgba sampleBackground(const ImageBgra& frame, const RectI& rect, int margin) {
    if (frame.empty()) {
        return Rgba{255, 255, 255, 255};
    }
    std::vector<std::uint8_t> blue;
    std::vector<std::uint8_t> green;
    std::vector<std::uint8_t> red;
    const auto take = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) {
            return;
        }
        const std::uint8_t* p = frame.pixel(x, y);
        blue.push_back(p[0]);
        green.push_back(p[1]);
        red.push_back(p[2]);
    };
    const int left = rect.left - margin;
    const int top = rect.top - margin;
    const int right = rect.right + margin - 1;
    const int bottom = rect.bottom + margin - 1;
    for (int x = left; x <= right; ++x) {
        take(x, top);
        take(x, bottom);
    }
    for (int y = top + 1; y < bottom; ++y) {
        take(left, y);
        take(right, y);
    }
    if (blue.empty()) {
        return Rgba{255, 255, 255, 255};
    }
    return Rgba{median(red), median(green), median(blue), 255};
}

double backgroundUniformity(const ImageBgra& frame, const RectI& rect, Rgba background, int margin,
                            int tolerance) {
    int total = 0;
    int close = 0;
    const auto take = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) {
            return;
        }
        const std::uint8_t* p = frame.pixel(x, y);
        ++total;
        if (std::abs(p[0] - background.b) <= tolerance &&
            std::abs(p[1] - background.g) <= tolerance &&
            std::abs(p[2] - background.r) <= tolerance) {
            ++close;
        }
    };
    const int left = rect.left - margin;
    const int top = rect.top - margin;
    const int right = rect.right + margin - 1;
    const int bottom = rect.bottom + margin - 1;
    for (int x = left; x <= right; ++x) {
        take(x, top);
        take(x, bottom);
    }
    for (int y = top + 1; y < bottom; ++y) {
        take(left, y);
        take(right, y);
    }
    return total == 0 ? 1.0 : static_cast<double>(close) / total;
}

Rgba readableTextColor(Rgba background) {
    // BT.601 亮度
    const int luminance = (77 * background.r + 150 * background.g + 29 * background.b) >> 8;
    return luminance >= 128 ? Rgba{0, 0, 0, 255} : Rgba{255, 255, 255, 255};
}

OverlayText overlayText(std::string_view translation) {
    OverlayText out;
    std::string_view body = translation;
    // 一般翻譯引擎的ルビ註解（net/ruby_notes）：「譯文　［本気（マジ）→ 認真（玩真的）］」
    std::vector<std::pair<std::string, std::string>> notes;
    if (const std::size_t open = body.rfind(kNotesOpen);
        open != std::string_view::npos && body.ends_with(kNotesClose)) {
        notes = parseRubyNotes(body.substr(
            open + kNotesOpen.size(), body.size() - open - kNotesOpen.size() - kNotesClose.size()));
        body = body.substr(0, open);
    }

    // LLM 留下的標記 `{認真|玩真的}`：本文留在正文，讀音變成ルビ
    std::size_t at = 0;
    for (const auto& [base, reading] : rubyMarkupPairs(body)) {
        const std::string marked = "{" + base + "|" + reading + "}";
        const std::size_t found = body.find(marked, at);
        if (found == std::string_view::npos) {
            break;
        }
        out.text.append(body.substr(at, found - at));
        out.ruby.push_back({characterCount(out.text), characterCount(base), reading});
        out.text += base;
        at = found + marked.size();
    }
    out.text.append(body.substr(at));

    // 註解：在正文裡找得到那個詞才標上去，找不到的只留在結果視窗
    for (const auto& [base, reading] : notes) {
        std::size_t from = 0;
        while ((from = out.text.find(base, from)) != std::string::npos) {
            const int start = characterCount(std::string_view(out.text).substr(0, from));
            const int length = characterCount(base);
            const bool overlaps = std::any_of(out.ruby.begin(), out.ruby.end(), [&](const auto& r) {
                return start < r.start + r.length && r.start < start + length;
            });
            if (!overlaps) {
                out.ruby.push_back({start, length, reading});
                break;
            }
            from += base.size();
        }
    }
    std::sort(out.ruby.begin(), out.ruby.end(),
              [](const OverlayRuby& a, const OverlayRuby& b) { return a.start < b.start; });
    return out;
}

void compositeOver(ImageBgra& below, const ImageBgra& above) {
    if (below.width != above.width || below.height != above.height) {
        return;
    }
    for (std::size_t i = 0; i + 3 < below.pixels.size(); i += 4) {
        const int alpha = above.pixels[i + 3];
        for (std::size_t channel = 0; channel < 3; ++channel) {
            // 預乘過的顏色：結果 = 上 + 下 × (1 − α)
            const int mixed =
                above.pixels[i + channel] + (below.pixels[i + channel] * (255 - alpha) + 127) / 255;
            below.pixels[i + channel] = static_cast<std::uint8_t>(std::min(mixed, 255));
        }
        below.pixels[i + 3] = 255;
    }
}

bool worthCovering(const ImageBgra& frame, const RectI& rect, Rgba background, float score) {
    if (score < kMinScore) {
        return false;
    }
    if (backgroundUniformity(frame, rect, background) >= kMinUniformity) {
        return true;
    }
    return score >= kMinScoreForLoose &&
           backgroundUniformity(frame, rect, background, 2, kLooseTolerance) >= kMinLooseUniformity;
}

bool worthInpainting(const TextBlock& block) {
    if (block.score < kMinScoreForInpainting && !block.inBubble) {
        return false;  // 把花紋讀成字的：修補了也只是抹掉原圖
    }
    return letterCount(block.text) >= kMinLettersForInpainting;
}

namespace {

// 要蓋住的範圍：原文的框往外多留一點邊，不超出畫面
RectI paddedRect(const ImageBgra& frame, const RectI& source) {
    return RectI{std::max(0, source.left - kPadding), std::max(0, source.top - kPadding),
                 std::min(frame.width, source.right + kPadding),
                 std::min(frame.height, source.bottom + kPadding)};
}

float minimumScore(const TextBlock& block) {
    return block.inBubble ? kMinScoreInBubble : kMinScore;
}

}  // namespace

bool untranslatedLatin(std::string_view source, std::string_view translation) {
    const ScriptCounts from = countScripts(source);
    const ScriptCounts to = countScripts(stripRubyMarkup(translation));
    const auto onlyLatin = [](const ScriptCounts& counts) {
        return counts.latin > 0 && counts.han == 0 && counts.kana == 0 && counts.hangul == 0;
    };
    return onlyLatin(from) && onlyLatin(to);
}

CoverDecision coverDecision(const ImageBgra& frame, const TextBlock& block, bool canInpaint) {
    if (looksLikeSymbols(frame, block)) {
        return CoverDecision::Symbols;
    }
    if (block.score < minimumScore(block)) {
        return CoverDecision::LowScore;
    }
    if (block.inBubble) {
        return CoverDecision::Cover;
    }
    const RectI rect = paddedRect(frame, block.rect);
    const Rgba background = sampleBackground(frame, rect);
    if (backgroundUniformity(frame, rect, background) >= kMinUniformity ||
        (canInpaint && worthInpainting(block)) ||
        worthCovering(frame, rect, background, block.score)) {
        return CoverDecision::Cover;
    }
    return CoverDecision::BusyBackground;
}

std::vector<OverlayItem> planOverlay(const ImageBgra& frame,
                                     std::span<const TranslatedBlock> groups, IInpainter* inpainter,
                                     OverlayDrops* drops) {
    OverlayDrops ignored;
    OverlayDrops& dropped = drops != nullptr ? *drops : ignored;
    std::vector<OverlayItem> items;
    for (const TranslatedBlock& group : groups) {
        if (group.translation.empty()) {
            ++dropped.untranslated;
            continue;  // 翻譯失敗：不蓋，原文照樣看得到
        }
        if (looksLikeSymbols(frame, group.block)) {
            ++dropped.symbols;
            continue;
        }
        if (untranslatedLatin(group.block.text, group.translation)) {
            ++dropped.unchanged;
            continue;
        }
        OverlayItem item;
        const RectI& source = group.block.rect;
        item.rect = paddedRect(frame, source);
        OverlayText text = overlayText(group.translation);
        item.text = std::move(text.text);
        item.ruby = std::move(text.ruby);
        item.vertical = group.block.orientation == Orientation::Vertical;
        item.background = sampleBackground(frame, item.rect);
        if (group.block.score < minimumScore(group.block)) {
            ++dropped.lowScore;
            continue;
        }
        const bool plain =
            backgroundUniformity(frame, item.rect, item.background) >= kMinUniformity;
        if (!plain && inpainter != nullptr && worthInpainting(group.block)) {
            // 背景不是純色：抹掉原文、補成周圍的樣子（M4-01）。補不出來就照下面的規則
            if (std::optional<ImageBgra> patch = inpainter->inpaint(frame, item.rect)) {
                item.patch = std::move(*patch);
            }
        }
        // 對話框裡的字一定蓋：雲朵、爆炸形的對話框，框的四角會切到邊線和背後的圖，
        // 量起來「背景不是純色」，以前整段被丟掉（網頁漫畫 34 頁裡有 41 段）
        if (item.patch.empty() && !group.block.inBubble &&
            !worthCovering(frame, item.rect, item.background, group.block.score)) {
            ++(group.block.score < kMinScore ? dropped.lowScore : dropped.busyBackground);
            continue;
        }
        // 照著原文的顏色畫（M4-02）；估計不出來（墨水太少、看不清楚）時用黑字或白字
        const std::optional<TextStyle> colors = estimateTextStyle(frame, source, item.background);
        if (colors) {
            item.foreground = colors->fill;
            item.outline = colors->outline;
        } else {
            item.foreground = readableTextColor(item.background);
        }
        if (!item.patch.empty()) {
            // 補出來的背景有圖案，背景色只是外圍的中位數，照它估的顏色不一定可靠：
            // 和補出來的背景對比不夠就改用黑字或白字，並一律加一圈相反顏色的描邊
            const Rgba under = averageColor(item.patch);
            if (contrastRatio(item.foreground, under) < kMinPatchContrast) {
                item.foreground = readableTextColor(under);
                item.outline.reset();
            }
            if (!item.outline) {
                item.outline = readableTextColor(item.foreground);
            }
        }
        item.size = group.block.size;
        item.lineThickness = lineThickness(group.block);
        item.soundEffect = group.block.soundEffect;
        items.push_back(std::move(item));
    }
    return items;
}

}  // namespace tmw::core
