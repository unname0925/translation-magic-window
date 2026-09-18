#pragma once

#include <cstdint>
#include <optional>

#include "core/geometry.h"
#include "core/image.h"

namespace tmw::core {

// 畫面來源：AutoTrigger 透過它取得螢幕縮圖。正式程式接到螢幕擷取，測試時換成假的來源。
class IFrameSource {
public:
    virtual ~IFrameSource() = default;

    // 目前最新畫面的序號。螢幕沒有任何變化時不會增加，用來避免重複取樣。
    virtual std::uint64_t frameSerial() const = 0;

    // 螢幕上 screenRect 範圍的灰階縮圖。失敗時回傳 std::nullopt。
    virtual std::optional<GrayImage> thumbnail(const RectI& screenRect) = 0;
};

}  // namespace tmw::core
