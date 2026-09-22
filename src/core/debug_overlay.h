// 除錯覆蓋框的內容（見 docs/design.md 4.12）。
//
// 打開之後，透鏡上會直接畫出 OCR 讀到的每一行、合併後的段落，以及狀態和耗時。
// 調整透鏡位置時馬上看得到「它到底讀到什麼、為什麼這樣分段」，不必每次都傾印出來看。
//
// 這個檔案只算出「要畫哪些框、寫哪些字」，實際畫在 platform/debug_overlay_window.h。
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/color.h"
#include "core/geometry.h"
#include "core/pipeline.h"

namespace tmw::core {

struct OverlayBox {
    RectI rect;  // 覆蓋框視窗的座標
    Rgba color;
    int thickness = 1;
};

struct DebugOverlay {
    std::vector<OverlayBox> boxes;
    // 左上角的文字，一行一個
    std::vector<std::string> status;

    bool empty() const { return boxes.empty() && status.empty(); }
};

// OCR 的一行：細的青色框
inline constexpr Rgba kOverlayLineColor{56, 189, 248, 255};
// 合併後的一段：粗的洋紅色框
inline constexpr Rgba kOverlayBlockColor{232, 121, 249, 255};

// result：最後一次處理的結果（`region` 是它當時的擷取範圍，螢幕座標）
// overlayScreenRect：覆蓋框視窗現在蓋住的範圍，螢幕座標
// stateName：觸發狀態機現在的狀態，例如「顯示中」
//
// 透鏡在處理完之後被移動過時，框會跟著平移到原來的位置上，不會黏在錯的地方。
DebugOverlay buildDebugOverlay(const PipelineResult& result, const RectI& overlayScreenRect,
                               const std::string& stateName);

// 還沒處理過任何東西時：只有狀態那一行
DebugOverlay buildDebugOverlay(const std::string& stateName);

// 把框畫進 pixels：size.width × size.height 個預乘 alpha 的 0xAARRGGBB，由上到下逐列排列。
// 緩衝區會先被清成全透明；超出範圍的框會被裁掉。
// pixels 的大小不符時丟出 std::invalid_argument。
void renderDebugOverlayBoxes(const DebugOverlay& overlay, SizeI size,
                             std::span<std::uint32_t> pixels);

}  // namespace tmw::core
