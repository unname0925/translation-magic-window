// 振り仮名（ルビ）的辨識與附著（見 docs/design.md 4.4）。
//
// OCR 看不出「這一行是ルビ」，它只是另一行小字。實測日文漫畫時，這些小字夾在主文的欄與欄之間，
// 讓「這兩欄是同一句」的判斷永遠斷掉，句子因此被切成碎片（400 組相鄰配對有 63% 被它擋下）。
//
// 所以在合併段落之前，先把明顯是ルビ的行挑出來，附到它標註的本文上。
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/text_layout.h"

namespace tmw::core {

struct RubyOptions {
    // ルビ 的字級要小於本文的這個倍數（實測中位數 0.46、最大 0.71）
    double maxSizeRatio = 0.75;
    // 和本文的距離上限，本文字級的倍數。實測的框常常和本文重疊（中位數 -0.17），
    // 所以這裡允許負值，只用上限把「隔壁那一欄」排除掉。
    double maxGapRatio = 1.2;
    // ルビ 要有這麼多落在本文的範圍內
    double minCoverRatio = 0.5;
    // 蓋住整欄的不是 ルビ，是另一句話
    double maxSpanRatio = 0.9;
};

struct RubyResult {
    // 本文（ルビ 已經附到 OcrLine::ruby 上）
    std::vector<OcrLine> lines;
    int attached = 0;
};

// 把 lines 中明顯是 ルビ 的行挑出來附到本文上，回傳剩下的行。
// 判斷依據：字比本文小很多、全是假名、緊貼著本文那一欄、只蓋住其中一部分。
RubyResult attachRuby(std::span<const OcrLine> lines, const RubyOptions& options = {});

// 這一行（或這一段）的文字是不是全部都是假名。ルビ 一定是假名。
bool isKanaOnly(std::string_view utf8);

// 把本文和 ルビ 合成送給翻譯引擎的標記：`{本文|讀音}`（design.md 4.5）。
// ruby 要依 start 由小到大排好，重疊的會被略過。
std::string markRuby(std::string_view text, std::span<const RubyAnnotation> ruby);

// 把 `{本文|讀音}` 還原成只有本文。看不懂標記的翻譯引擎（Google、DeepL）要先用它，
// 否則譯文裡的標記數量會對不上，對齊檢查會一直判定格式錯誤（design.md 4.5）。
std::string stripRubyMarkup(std::string_view text);

}  // namespace tmw::core
