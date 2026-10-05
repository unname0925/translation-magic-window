// 譯文蓋在原文位置上的一段（M3）。規劃在 core/overlay_plan，繪製在 platform/overlay_renderer。
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/color.h"
#include "core/geometry.h"
#include "core/image.h"
#include "core/text_layout.h"

namespace tmw::core {

// 譯文裡用小字標在詞旁邊的讀音（另有含義的ルビ，design.md 4.8）：
// 正文的第 start 個字起、共 length 個字，旁邊標 text。位置以「字」計，不是位元組。
struct OverlayRuby {
    int start = 0;
    int length = 0;
    std::string text;

    friend bool operator==(const OverlayRuby&, const OverlayRuby&) = default;
};

struct OverlayItem {
    RectI rect;  // 要蓋住的範圍（畫面座標，已經往外多留一點邊、不超出畫面）
    std::string text;
    std::vector<OverlayRuby> ruby;
    bool vertical = false;
    Rgba background;
    // 背景修補的結果（rect 大小，M4-01）。空的代表用 background 純色填滿
    ImageBgra patch;
    Rgba foreground;
    // 原文有描邊時的描邊顏色（M4-02）
    std::optional<Rgba> outline;
    TextSize size = TextSize::Normal;
    // 原文一行的粗細（像素，各行的中位數）。譯文的字不比它大：短短的譯文放進大框時，
    // 不會被放大成標題。0 = 不知道，只受框的大小限制。
    int lineThickness = 0;
    // 原文看起來是擬聲字（網頁的控制面板可以馬上把它藏起來，不必重翻）
    bool soundEffect = false;

    friend bool operator==(const OverlayItem&, const OverlayItem&) = default;
};

// planOverlay 沒蓋的段落依原因計數（記錄裡看得到譯文為什麼少了幾段）
struct OverlayDrops {
    int untranslated = 0;    // 沒有譯文（翻譯失敗、引擎回傳空的）
    int lowScore = 0;        // OCR 分數太低
    int busyBackground = 0;  // 背景不是純色、沒有修補，不值得蓋

    int total() const { return untranslated + lowScore + busyBackground; }
};

}  // namespace tmw::core
