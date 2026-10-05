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
#include "core/inpainter.h"
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

struct OverlayText {
    std::string text;
    std::vector<OverlayRuby> ruby;  // 依位置排好
};

// 譯文拆成正文和ルビ：LLM 留下的 `{認真|玩真的}` 標記，以及一般翻譯引擎接在後面的註解
// 「　［本気（マジ）→ 認真（玩真的）］」，都變成「認真」旁邊標小字「玩真的」。
// 註解裡的詞在正文找不到時就不標（結果視窗裡還看得到），註解本身不畫：
// 一長串註解會把對話框裡的字擠得很小。
OverlayText overlayText(std::string_view translation);

// 把預乘 alpha 的 above（OverlayRenderer 畫出來的）疊到 below 上，
// 結果就是使用者在螢幕上看到的樣子。tmw_overlay_preview 用它：
// 覆蓋層排除在擷取之外，螢幕截圖看不到它。兩張圖大小要一樣，否則 below 不變。
void compositeOver(ImageBgra& below, const ImageBgra& above);

// 背景不是純色時，值不值得用修補把原文抹掉：OCR 很有把握（或對話框偵測器框到的文字），
// 而且至少有兩個字（擬聲詞、「！」、把花紋讀成的字不修補，免得抹掉原圖）
bool worthInpainting(const TextBlock& block);

// 翻譯之前就能判斷的：這一段會不會蓋上去（和 planOverlay 同樣的規則，只差在要修補的
// 假設會補成功）。網頁漫畫沒有結果視窗，蓋不上去的段落翻了也看不到，不送翻譯
enum class CoverDecision { Cover, LowScore, BusyBackground };
CoverDecision coverDecision(const ImageBgra& frame, const TextBlock& block, bool canInpaint);

// 每一段譯文要怎麼蓋。
// - 背景是純色：用背景色填滿
// - 背景不是純色、有 inpainter、而且 worthInpainting：抹掉原文補成周圍的樣子（OverlayItem::patch）
// - 其餘照 worthCovering：遊戲的半透明對話框用純色，擬聲詞和畫在圖上的字不蓋
// 沒有譯文的段落（翻譯失敗）不蓋，原文照樣看得到。
// 沒蓋的段落依原因計數在 drops（OverlayDrops，core/overlay_item.h）
std::vector<OverlayItem> planOverlay(const ImageBgra& frame,
                                     std::span<const TranslatedBlock> groups,
                                     IInpainter* inpainter = nullptr,
                                     OverlayDrops* drops = nullptr);

}  // namespace tmw::core
