// 估計原文的文字顏色和描邊顏色（M4-02，design.md 4.8「風格估計」），譯文照著畫。
//
// 框內和背景色差很多的像素算「墨水」：
// - 填色：離背景最深的兩成墨水（筆畫正中央）的中位數。反鋸齒的淡邊和描邊都在外圍
// - 描邊：貼著背景的邊緣像素有三成以上不是「填色和背景之間的漸層」（反鋸齒），而且填色
//   幾乎不直接碰到背景（描邊把兩者隔開），那個第三種顏色就是描邊
// 墨水太少、或填色和背景太接近又沒有描邊（看不清楚）時估計不出來，呼叫端改用黑字或白字。
#pragma once

#include <optional>

#include "core/color.h"
#include "core/geometry.h"
#include "core/image.h"

namespace tmw::core {

struct TextStyle {
    Rgba fill;
    std::optional<Rgba> outline;

    friend bool operator==(const TextStyle&, const TextStyle&) = default;
};

// 兩個顏色的亮度對比（WCAG，1～21）。3 以上大字讀得清楚
double contrastRatio(Rgba a, Rgba b);

std::optional<TextStyle> estimateTextStyle(const ImageBgra& frame, const RectI& rect,
                                           Rgba background);

}  // namespace tmw::core
