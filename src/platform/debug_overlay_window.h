// 除錯覆蓋框的視窗（見 docs/design.md 4.12）。
//
// 蓋在透鏡上，畫出 OCR 讀到的每一行、合併後的段落，以及狀態和耗時。
// 和透鏡一樣：點擊完全穿透、不搶焦點，而且**不會出現在任何螢幕擷取中**——
// 否則下一次 OCR 就會讀到自己畫的字。
//
// 要畫什麼由 core/debug_overlay 決定，這裡只負責把它畫出來。
#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

#include "core/debug_overlay.h"
#include "core/geometry.h"

namespace tmw::platform {

class DebugOverlayWindow {
public:
    // 建立失敗時丟出 std::runtime_error
    explicit DebugOverlayWindow(HINSTANCE instance);
    ~DebugOverlayWindow();

    DebugOverlayWindow(const DebugOverlayWindow&) = delete;
    DebugOverlayWindow& operator=(const DebugOverlayWindow&) = delete;

    HWND hwnd() const { return hwnd_; }

    // 蓋住 screenRect（螢幕座標、實體像素）並畫出 overlay
    void update(const core::RectI& screenRect, const core::DebugOverlay& overlay);
    void hide();
    bool isVisible() const;

    bool isExcludedFromCapture() const;

    static constexpr wchar_t kClassName[] = L"TranslationMagicWindow.DebugOverlay";

private:
    static void registerWindowClass(HINSTANCE instance);
    void ensureBitmap(core::SizeI size);
    void releaseBitmap();
    // 左上角的資訊面板。回傳它佔掉的範圍，之後要把那塊變成不透明。
    core::RectI drawStatusPanel(const core::DebugOverlay& overlay, core::SizeI size);

    HWND hwnd_ = nullptr;
    HDC memoryDc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previousBitmap_ = nullptr;
    HFONT font_ = nullptr;
    std::uint32_t* bits_ = nullptr;
    core::SizeI bitmapSize_{};
};

}  // namespace tmw::platform
