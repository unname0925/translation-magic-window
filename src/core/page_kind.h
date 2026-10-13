// 網頁漫畫的一頁是不是雜誌廣告（使用者：廣告不需要翻譯）。
//
// soraraw 的雜誌掃描每章後面常接十幾頁廣告（周邊商品、卡牌、展覽、投稿募集）。這些頁一頁
// 25～49 段、翻譯要 1～2 分鐘，拖慢整章，又沒人要看。
// 2026-10-13 量《カグラバチ》118 話（38 頁：漫畫 19 頁、廣告 19 頁），辨識完的段落：
//
// |               | 漫畫頁    | 廣告頁                                    |
// |---------------|-----------|-------------------------------------------|
// | 字數          | 1～94     | 245～855                                  |
// | 段數          | 1～12     | 15～49                                    |
// | 橫排的字      | 多半 0～3 成 | 多半 6～10 成（直排的卡牌廣告 2.6 成）  |
// | 彩色的像素    | 1.3 成    | 黑白廣告 1.3～1.4 成，粉紅雙色 3.1～7.1 成 |
// | 對話框內的字  | 5～10 成  | 1.4～8.4 成（廣告的文字框也常被當成對話框） |
//
// 規則：字數 ≥ 200、段數 ≥ 15，而且（橫排的字過半、彩色像素 ≥ 3 成、或對話框內的字不到一半）。
// 這一章 19 頁廣告全部認出、漫畫頁一頁都沒有誤判。字很多的漫畫頁多半是直排、黑白、在對話框裡，
// 不會被當成廣告；字很多的彩色漫畫頁會（少見，開頭的彩頁字通常很少）。
#pragma once

#include <span>

#include "core/image.h"
#include "core/text_layout.h"

namespace tmw::core {

struct PageTextStats {
    int segments = 0;
    int characters = 0;
    double horizontalShare = 0.0;  // 橫排的字占幾成
    double bubbleShare = 0.0;      // 對話框內的字占幾成
    double colorShare = 0.0;       // 彩色的像素占幾成（飽和度夠高、不太暗）
};

PageTextStats pageTextStats(const ImageBgra& frame, std::span<const TextBlock> blocks);

bool looksLikeAdPage(const PageTextStats& stats);

}  // namespace tmw::core
