// 把譯文蓋在原文位置上的視窗（M3-01，design.md 4.8）。
//
// 和除錯覆蓋框一樣：點擊完全穿透、不搶焦點，而且不會出現在任何螢幕擷取中——
// 否則下一次 OCR 讀到的就是自己畫上去的譯文。
// 要畫成什麼樣子由 OverlayRenderer 決定，這裡只負責把那張圖貼到螢幕上。
#pragma once

#include <windows.h>

#include <cstdint>

#include "core/geometry.h"
#include "core/image.h"

namespace tmw::platform {

class TranslationOverlayWindow {
public:
    // 建立失敗時丟出 std::runtime_error
    explicit TranslationOverlayWindow(HINSTANCE instance);
    ~TranslationOverlayWindow();

    TranslationOverlayWindow(const TranslationOverlayWindow&) = delete;
    TranslationOverlayWindow& operator=(const TranslationOverlayWindow&) = delete;

    HWND hwnd() const { return hwnd_; }

    // 把 image（預乘 alpha 的 BGRA）貼在螢幕上的 topLeft
    void show(core::PointI topLeft, const core::ImageBgra& image);
    void hide();
    bool isVisible() const;

    bool isExcludedFromCapture() const;

    static constexpr wchar_t kClassName[] = L"TranslationMagicWindow.TranslationOverlay";

private:
    void ensureBitmap(core::SizeI size);
    void releaseBitmap();

    HWND hwnd_ = nullptr;
    HDC memoryDc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previousBitmap_ = nullptr;
    std::uint8_t* bits_ = nullptr;
    core::SizeI bitmapSize_{};
};

}  // namespace tmw::platform
