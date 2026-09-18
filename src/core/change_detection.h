#pragma once

#include "core/image.h"

namespace tmw::core {

// 判斷兩張縮圖之間「畫面有沒有變化」的門檻（見 docs/design.md 4.3）。
struct ChangeThresholds {
    // 單一縮圖像素的亮度差超過這個值，才算這個像素「變了」。
    // 用來忽略微小的雜訊。
    int pixelDelta = 12;
    // 變了的像素達到這個數量，就算畫面有變化。
    // 縮圖大約是原圖的 1/8，一個新出現的字大概就佔 1～4 個縮圖像素，所以預設是 1。
    int minChangedPixels = 1;
    // 或者：整張縮圖的平均亮度差超過這個值（抓整體的淡入淡出，每個像素只變一點點的情況）。
    double meanDelta = 3.0;
};

struct ChangeMetrics {
    int changedPixels = 0;
    double meanAbsDelta = 0.0;
};

// 兩張大小相同的縮圖之間的差異。大小不同時丟出 std::invalid_argument。
ChangeMetrics measureChange(const GrayImage& before, const GrayImage& after, int pixelDelta);

// 畫面是否有變化。大小不同（例如透鏡被縮放）一律算有變化。
bool contentChanged(const GrayImage& before, const GrayImage& after,
                    const ChangeThresholds& thresholds = {});

// BGRA 轉灰階（BT.601 亮度）。
GrayImage toGray(const ImageBgra& image);

}  // namespace tmw::core
