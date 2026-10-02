#include "platform/translation_overlay_window.h"

#include <cstring>

#include "platform/win_error.h"

namespace tmw::platform {
namespace {

void registerWindowClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = TranslationOverlayWindow::kClassName;
    // 已經註冊過不算錯誤（關掉再打開時會再走一次）
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throwLastError("RegisterClassExW failed for the translation overlay");
    }
}

}  // namespace

TranslationOverlayWindow::TranslationOverlayWindow(HINSTANCE instance) {
    registerWindowClass(instance);

    // WS_EX_TRANSPARENT：滑鼠完全穿透，譯文底下的按鈕照樣點得到
    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kClassName, L"譯文覆蓋層", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (hwnd_ == nullptr) {
        throwLastError("CreateWindowExW failed for the translation overlay");
    }
    // 譯文如果被擷取進去，下一次 OCR 讀到的就是譯文
    if (!SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE)) {
        const DWORD error = GetLastError();
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        SetLastError(error);
        throwLastError("SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed");
    }
    memoryDc_ = CreateCompatibleDC(nullptr);
    if (memoryDc_ == nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        throwLastError("CreateCompatibleDC failed for the translation overlay");
    }
}

TranslationOverlayWindow::~TranslationOverlayWindow() {
    releaseBitmap();
    if (memoryDc_ != nullptr) {
        DeleteDC(memoryDc_);
    }
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
}

bool TranslationOverlayWindow::isVisible() const {
    return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE;
}

bool TranslationOverlayWindow::isExcludedFromCapture() const {
    DWORD affinity = 0;
    return hwnd_ != nullptr && GetWindowDisplayAffinity(hwnd_, &affinity) &&
           affinity == WDA_EXCLUDEFROMCAPTURE;
}

void TranslationOverlayWindow::hide() {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void TranslationOverlayWindow::ensureBitmap(core::SizeI size) {
    if (bitmap_ != nullptr && size == bitmapSize_) {
        return;
    }
    releaseBitmap();

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = size.width;
    info.bmiHeader.biHeight = -size.height;  // 負值：由上到下排列
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    bitmap_ = CreateDIBSection(memoryDc_, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap_ == nullptr) {
        throwLastError("CreateDIBSection failed for the translation overlay");
    }
    bits_ = static_cast<std::uint8_t*>(bits);
    bitmapSize_ = size;
    previousBitmap_ = SelectObject(memoryDc_, bitmap_);
}

void TranslationOverlayWindow::releaseBitmap() {
    if (bitmap_ == nullptr) {
        return;
    }
    SelectObject(memoryDc_, previousBitmap_);
    DeleteObject(bitmap_);
    bitmap_ = nullptr;
    bits_ = nullptr;
    bitmapSize_ = {};
}

void TranslationOverlayWindow::show(core::PointI topLeft, const core::ImageBgra& image) {
    if (hwnd_ == nullptr || image.empty()) {
        hide();
        return;
    }
    const core::SizeI size{image.width, image.height};
    ensureBitmap(size);
    std::memcpy(bits_, image.pixels.data(), image.pixels.size());

    POINT source{0, 0};
    POINT destination{topLeft.x, topLeft.y};
    SIZE windowSize{size.width, size.height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, nullptr, &destination, &windowSize, memoryDc_, &source, 0, &blend,
                        ULW_ALPHA);
    // 拖過透鏡之後透鏡會跑到上面；每次都拉回最上層，譯文才不會被透鏡的框蓋住
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

}  // namespace tmw::platform
