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
#include <utility>
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
    // 讀錯的 ルビ（沒有假名也沒有漢字，例如「11.5」）要比本文那一欄細這麼多才丟掉。
    // 比的是欄寬：讀錯的文字字數沒有意義，不能用「欄長 ÷ 字數」估字級。
    // 實測讀錯的 ルビ 寬 10～18 像素，旁邊的本文 35～37 像素。
    double maxMisreadWidthRatio = 0.6;
};

struct RubyResult {
    // 本文（ルビ 已經附到 OcrLine::ruby 上）
    std::vector<OcrLine> lines;
    int attached = 0;
    // 位置和大小像 ルビ、字卻不是假名而被丟掉的行（OCR 把小小的假名讀成數字之類）
    int dropped = 0;
};

// 把 lines 中明顯是 ルビ 的行挑出來附到本文上，回傳剩下的行。
// 判斷依據：字比本文小很多、緊貼著本文那一欄、只蓋住其中一部分，而且全是假名。
// 前三項都符合、字卻不是假名的，是讀錯的 ルビ：丟掉，不附上也不留下（見 dropped）。
RubyResult attachRuby(std::span<const OcrLine> lines, const RubyOptions& options = {});

// 這一行（或這一段）的文字是不是全部都是假名。ルビ 一定是假名。
bool isKanaOnly(std::string_view utf8);

// 把本文和 ルビ 合成送給翻譯引擎的標記：`{本文|讀音}`（design.md 4.5）。
// ruby 要依 start 由小到大排好，重疊的會被略過。
std::string markRuby(std::string_view text, std::span<const RubyAnnotation> ruby);

// 這個讀音是不是作者刻意的特殊讀音（本気=マジ），而不是一般的振り仮名（東京=とうきょう）。
// 一般的振り仮名用平假名；特殊讀音多半是片假名。漫畫裡 98% 的ルビ是一般讀音
// （日文漫畫正確答案 173 個裡只有 3 個特殊，這條規則全部分對）。
// 已知會漏掉用平假名寫的特殊讀音（強敵=とも）：正式的做法要查辭典（M2-13）。
bool isSpecialReading(std::string_view reading);

// 送給翻譯引擎的文字：只標記特殊讀音（M2-14）。一般讀音的標記沒有意義，還會害本機的
// hy-mt2 整批不翻、原文照回（實測「{東京|とうきょう}に行くんだ」原封不動地回來）。
// 結果視窗顯示原文時仍然用 markRuby 顯示全部ルビ。
std::string markSpecialRuby(std::string_view text, std::span<const RubyAnnotation> ruby);

// 一段文字上標好的 ルビ，搬到同一段話的另一個版本上（manga-ocr 重讀出來的文字，M2-03）。
// 位置是「第幾個字」，兩個版本的字數不一定一樣（PP-OCR 多讀或少讀了字），所以不能照抄位置：
// 拿每個 ルビ 的本文（例如「楓林」）到新的文字裡找，從上一個 ルビ 之後開始找第一個出現的地方。
// 新文字裡找不到那個本文（manga-ocr 讀成別的字）就丟掉那個 ルビ。ruby 要依 start 排好。
std::vector<RubyAnnotation> remapRuby(std::string_view oldText,
                                      std::span<const RubyAnnotation> ruby,
                                      std::string_view newText);

// 把 `{本文|讀音}` 還原成只有本文。看不懂標記的翻譯引擎（Google、DeepL）要先用它，
// 否則譯文裡的標記數量會對不上，對齊檢查會一直判定格式錯誤（design.md 4.5）。
std::string stripRubyMarkup(std::string_view text);

// 文字裡每個 `{本文|讀音}` 的（本文, 讀音），依出現順序。沒有配對的大括號不算。
std::vector<std::pair<std::string, std::string>> rubyMarkupPairs(std::string_view text);

}  // namespace tmw::core
