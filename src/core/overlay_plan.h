// 把譯文直接蓋在原文位置上（M3，design.md 4.8）：每一段要蓋在哪、用什麼顏色。
//
// 這裡只決定「畫什麼」，真正的繪製（DirectWrite、自動縮放字級）在 platform/overlay_renderer。
// - 背景色：取原文框外圍一圈像素的中位數（M3-02，純色背景用；網點、漸層背景要等 M4 的修補）
// - 文字色：在背景上看得清楚的黑或白
// - 方向：直排的段落譯文也直排（漫畫對話框），其餘橫排
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/image.h"
#include "core/overlay_item.h"
#include "core/pipeline.h"

namespace tmw::core {

// 框外圍一圈（往外 margin 像素那一圈）的顏色中位數，各通道分開取。框貼著畫面邊時只取畫面內的。
Rgba sampleBackground(const ImageBgra& frame, const RectI& rect, int margin = 2);

// 框外圍一圈有多少比例的像素接近 background（各通道相差不到 tolerance）。
// 對話框、文字框是純色的，接近 1；畫在圖上的字（擬聲詞、招牌）周圍是畫，低很多。
double backgroundUniformity(const ImageBgra& frame, const RectI& rect, Rgba background,
                            int margin = 2, int tolerance = 24);

// 這一段要不要用純色蓋掉：背景要夠純（半透明的遊戲對話框要 OCR 很有把握），而且 OCR 的分數
// 不能太低（多半是把圖示讀成了字）。門檻和量測結果見 overlay_plan.cpp。
bool worthCovering(const ImageBgra& frame, const RectI& rect, Rgba background, float score);

// 在 background 上讀得清楚的文字顏色：亮的背景用黑字，暗的用白字
Rgba readableTextColor(Rgba background);

// 譯文裡的 `{本文|讀音}`（另有含義的ルビ）先畫成「本文（讀音）」
std::string overlayText(std::string_view translation);

// 把預乘 alpha 的 above（OverlayRenderer 畫出來的）疊到 below 上，
// 結果就是使用者在螢幕上看到的樣子。tmw_overlay_preview 用它：
// 覆蓋層排除在擷取之外，螢幕截圖看不到它。兩張圖大小要一樣，否則 below 不變。
void compositeOver(ImageBgra& below, const ImageBgra& above);

// 每一段譯文要怎麼蓋。沒有譯文的段落（翻譯失敗）和背景不是純色的段落（擬聲詞、畫在圖上的字，
// 用純色蓋上去會是一塊色塊）不蓋，原文照樣看得到。
std::vector<OverlayItem> planOverlay(const ImageBgra& frame,
                                     std::span<const TranslatedBlock> groups);

}  // namespace tmw::core
