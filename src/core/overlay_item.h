// 譯文蓋在原文位置上的一段（M3）。規劃在 core/overlay_plan，繪製在 platform/overlay_renderer。
#pragma once

#include <string>

#include "core/color.h"
#include "core/geometry.h"
#include "core/text_layout.h"

namespace tmw::core {

struct OverlayItem {
    RectI rect;  // 要蓋住的範圍（畫面座標，已經往外多留一點邊、不超出畫面）
    std::string text;
    bool vertical = false;
    Rgba background;
    Rgba foreground;
    TextSize size = TextSize::Normal;
    // 原文一行的粗細（像素，各行的中位數）。譯文的字不比它大：短短的譯文放進大框時，
    // 不會被放大成標題。0 = 不知道，只受框的大小限制。
    int lineThickness = 0;

    friend bool operator==(const OverlayItem&, const OverlayItem&) = default;
};

}  // namespace tmw::core
