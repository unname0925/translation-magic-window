#pragma once

#include <utility>

#include "core/frame_source.h"

namespace tmw::test {

// 測試用的畫面來源：內容由測試直接指定，每次 setContent 都會讓畫面序號加一，
// 就像螢幕真的送來一張新畫面。
class FakeFrameSource final : public core::IFrameSource {
public:
    void setContent(core::GrayImage content) {
        content_ = std::move(content);
        ++serial_;
    }
    void setFailing(bool failing) { failing_ = failing; }

    std::uint64_t frameSerial() const override { return serial_; }

    std::optional<core::GrayImage> thumbnail(const core::RectI& screenRect) override {
        ++thumbnailCalls_;
        lastRegion_ = screenRect;
        if (failing_) {
            return std::nullopt;
        }
        return content_;
    }

    int thumbnailCalls() const { return thumbnailCalls_; }
    const core::RectI& lastRegion() const { return lastRegion_; }

private:
    core::GrayImage content_{8, 8, 128};
    std::uint64_t serial_ = 1;
    bool failing_ = false;
    int thumbnailCalls_ = 0;
    core::RectI lastRegion_;
};

}  // namespace tmw::test
