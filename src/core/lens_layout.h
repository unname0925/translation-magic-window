#pragma once

#include <cstdint>
#include <span>

#include "core/color.h"
#include "core/geometry.h"

namespace tmw::core {

// 透鏡視窗各部位的尺寸，以 96 DPI（100% 縮放）為基準。
struct LensMetrics {
    int outerGrab = 6;     // 可見邊框外側的抓取區
    int border = 3;        // 可見邊框
    int innerGrab = 6;     // 可見邊框內側的抓取區
    int handleWidth = 72;  // 左上方的拖動把手
    int handleHeight = 22;
    int corner = 16;  // 角落的縮放區，沿著邊延伸的長度
    int gripDot = 2;  // 把手上的小圓點
    int minContentWidth = 120;
    int minContentHeight = 60;
};

LensMetrics scaledLensMetrics(unsigned dpi);

// 透鏡視窗的版面，座標是「視窗內」的實體像素（見 docs/design.md 4.1）。
//
//   ┌──────┐
//   │ 把手 │                    ← handle：拖動整個透鏡
//   ├──────┴──────────────┐    ← border：可見邊框的外緣
//   │ ┌─────────────────┐ │    ← content：可見邊框的內緣，也就是擷取範圍
//   │ │ ┌─────────────┐ │ │    ← clickThrough：完全透明，滑鼠會穿透到底下的程式
//   │ │ │             │ │ │
//
// 可見邊框的外側和內側各有一圈透明度 1/255 的抓取區：肉眼看不到，但點得到，
// 讓細邊框也容易抓。frame 是包含外側抓取區在內的透鏡本體外緣。
struct LensLayout {
    SizeI window;
    RectI handle;
    RectI frame;
    RectI border;
    RectI content;
    RectI clickThrough;
    int cornerSize = 0;
    int gripDot = 0;
};

LensLayout computeLensLayout(SizeI window, unsigned dpi);

// 要讓擷取範圍（content）是指定大小時，整個視窗需要多大。
SizeI lensWindowSizeForContent(SizeI content, unsigned dpi);

SizeI minimumLensWindowSize(unsigned dpi);

enum class LensHitZone {
    Transparent,  // 滑鼠穿透
    Move,         // 拖動把手
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

LensHitZone hitTestLens(const LensLayout& layout, PointI point);

// 把透鏡畫進 pixels：window.width × window.height 個預乘 alpha 的 0xAARRGGBB，由上到下逐列排列。
// 保證「alpha 不為 0 的像素」和「hitTestLens 不是 Transparent 的位置」完全一致。
// pixels 的大小不符時丟出 std::invalid_argument。
void renderLens(const LensLayout& layout, Rgba accent, std::span<std::uint32_t> pixels);

}  // namespace tmw::core
