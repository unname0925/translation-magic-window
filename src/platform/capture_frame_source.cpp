#include "platform/capture_frame_source.h"

#include <chrono>

#include "core/change_detection.h"

namespace tmw::platform {

std::uint64_t CaptureFrameSource::frameSerial() const {
    return capture_.stats().framesArrived;
}

std::optional<core::GrayImage> CaptureFrameSource::thumbnail(const core::RectI& screenRect) {
    // 在 UI 執行緒上呼叫，所以只等一下第一張畫面；等不到就下次 tick 再試
    const std::optional<core::ImageBgra> image =
        capture_.readThumbnail(screenRect, kThumbnailMaxSide, std::chrono::milliseconds{300});
    if (!image) {
        return std::nullopt;
    }
    return core::toGray(*image);
}

}  // namespace tmw::platform
