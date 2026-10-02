// 背景修補（M4-01，design.md 4.8）：把原文從畫面上抹掉、補成周圍的樣子，譯文再畫在上面。
// 背景不是純色時（遊戲的半透明對話框、網點上的字）用它；實作在
// ocr/lama_inpainter（LaMa，只用顯示卡）。
#pragma once

#include <optional>

#include "core/geometry.h"
#include "core/image.h"

namespace tmw::core {

class IInpainter {
public:
    virtual ~IInpainter() = default;

    // frame 上 rect 的範圍補成周圍的樣子，回傳 rect 大小的圖（BGRA，不透明）。
    // 做不到時（模型載入失敗、沒有顯示卡）回傳 nullopt，呼叫端照舊處理。
    virtual std::optional<ImageBgra> inpaint(const ImageBgra& frame, const RectI& rect) = 0;
};

}  // namespace tmw::core
