#pragma once

#include "core/frame_source.h"
#include "platform/screen_capture.h"

namespace tmw::platform {

// 把螢幕擷取接到 core::AutoTrigger：畫面序號就是擷取收到的畫面數，
// 縮圖在 GPU 上縮小後才讀回，再轉成灰階。
class CaptureFrameSource final : public core::IFrameSource {
public:
    // 縮圖最長邊的上限。大約是原圖的 1/8，一個字大概佔 1～4 個縮圖像素。
    static constexpr int kThumbnailMaxSide = 128;

    explicit CaptureFrameSource(ScreenCapture& capture) : capture_(capture) {}

    std::uint64_t frameSerial() const override;
    std::optional<core::GrayImage> thumbnail(const core::RectI& screenRect) override;

private:
    ScreenCapture& capture_;
};

}  // namespace tmw::platform
