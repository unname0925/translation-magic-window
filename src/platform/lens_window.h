#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>

#include "core/color.h"
#include "core/geometry.h"
#include "core/lens_layout.h"

namespace tmw::platform {

// 透鏡視窗：邊框可以拖動和縮放，中間點擊穿透，而且不會出現在任何螢幕擷取中。
// 外觀和點擊範圍由 core/lens_layout 決定（見 docs/design.md 4.1）。
class LensWindow {
public:
    struct Callbacks {
        std::function<void()> onMoveSizeStart;  // 開始拖動或縮放
        std::function<void()> onMoveSizeEnd;    // 放開滑鼠
    };

    // 在滑鼠所在螢幕的中央建立透鏡，擷取範圍的大小以 96 DPI 為基準。
    // 建立失敗時丟出 std::runtime_error。
    LensWindow(HINSTANCE instance, core::SizeI contentSizeAt96Dpi, Callbacks callbacks);
    ~LensWindow();

    LensWindow(const LensWindow&) = delete;
    LensWindow& operator=(const LensWindow&) = delete;

    HWND hwnd() const { return hwnd_; }

    bool isVisible() const;
    void setVisible(bool visible);

    void setAccent(core::Rgba accent);

    // 擷取範圍（可見邊框的內緣），螢幕座標、實體像素。
    core::RectI contentScreenRect() const;

    bool isExcludedFromCapture() const;

    static constexpr core::Rgba kDefaultAccent{59, 130, 246, 230};

private:
    static void registerWindowClass(HINSTANCE instance);
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void destroy();
    unsigned dpi() const;
    core::RectI windowRect() const;
    core::LensLayout currentLayout() const;
    void ensureBitmap(core::SizeI size);
    void releaseBitmap();
    void render();

    HWND hwnd_ = nullptr;
    Callbacks callbacks_;
    core::Rgba accent_ = kDefaultAccent;

    // UpdateLayeredWindow 使用的 32 位元 DIB（預乘 alpha）
    HDC memoryDc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previousBitmap_ = nullptr;
    std::uint32_t* bits_ = nullptr;
    core::SizeI bitmapSize_;
};

}  // namespace tmw::platform
