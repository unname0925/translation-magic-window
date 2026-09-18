#pragma once

namespace tmw::core {

struct PointI {
    int x = 0;
    int y = 0;

    friend constexpr bool operator==(const PointI&, const PointI&) = default;
};

// 採用和 Win32 RECT 相同的慣例：left、top 包含在內，right、bottom 不包含。
// 座標一律是實體像素。
struct RectI {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    static constexpr RectI fromXYWH(int x, int y, int width, int height) {
        return {x, y, x + width, y + height};
    }

    constexpr int width() const { return right - left; }
    constexpr int height() const { return bottom - top; }
    constexpr bool empty() const { return right <= left || bottom <= top; }

    constexpr bool contains(PointI p) const {
        return p.x >= left && p.x < right && p.y >= top && p.y < bottom;
    }

    constexpr PointI center() const { return {left + width() / 2, top + height() / 2}; }

    friend constexpr bool operator==(const RectI&, const RectI&) = default;
};

// 兩個矩形的交集。沒有交集（包含只有邊相接）時回傳空矩形 RectI{}。
RectI intersect(const RectI& a, const RectI& b);

RectI offset(const RectI& rect, int dx, int dy);

// 四邊各往外擴 amount 像素；amount 為負值時往內縮。縮到沒有面積時回傳空矩形。
RectI inflate(const RectI& rect, int amount);

// 把螢幕座標的範圍轉成「該螢幕擷取畫面」內的座標，並裁掉超出螢幕的部分。
// 例如副螢幕在主螢幕左邊時，螢幕座標是負的，但擷取畫面的座標從 0 開始。
// 範圍完全不在這個螢幕上時回傳空矩形。
RectI screenToMonitorLocal(const RectI& screenRect, const RectI& monitorRect);

// 以 96 DPI（100% 縮放）為基準的長度，換算成指定 DPI 下的長度，四捨五入。
// 例如 8px 在 144 DPI（150%）下是 12px。
int scaleForDpi(int value, unsigned dpi);

}  // namespace tmw::core
