#include "platform/debug_overlay_window.h"

#include <algorithm>
#include <span>
#include <string>

#include "platform/text_encoding.h"
#include "platform/win_error.h"

namespace tmw::platform {
namespace {

constexpr int kPanelPadding = 8;
constexpr COLORREF kPanelBackground = RGB(17, 24, 39);
constexpr COLORREF kPanelText = RGB(226, 232, 240);
constexpr int kFontHeightAt96Dpi = -14;

unsigned dpiOf(HWND hwnd) {
    const unsigned dpi = GetDpiForWindow(hwnd);
    return dpi == 0 ? 96 : dpi;
}

}  // namespace

void DebugOverlayWindow::registerWindowClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kClassName;
    // 已經註冊過不算錯誤（重開覆蓋框時會再走一次）
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throwLastError("RegisterClassExW failed for the debug overlay");
    }
}

DebugOverlayWindow::DebugOverlayWindow(HINSTANCE instance) {
    registerWindowClass(instance);

    // WS_EX_TRANSPARENT：滑鼠完全穿透，覆蓋框不會擋到底下的任何操作
    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kClassName, L"除錯覆蓋框", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (hwnd_ == nullptr) {
        throwLastError("CreateWindowExW failed for the debug overlay");
    }
    // 自己畫的框如果被擷取進去，下一次 OCR 就會讀到它
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
        throwLastError("CreateCompatibleDC failed for the debug overlay");
    }

    const int height = MulDiv(kFontHeightAt96Dpi, static_cast<int>(dpiOf(hwnd_)), 96);
    font_ = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei UI");
}

DebugOverlayWindow::~DebugOverlayWindow() {
    releaseBitmap();
    if (font_ != nullptr) {
        DeleteObject(font_);
    }
    if (memoryDc_ != nullptr) {
        DeleteDC(memoryDc_);
    }
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
}

bool DebugOverlayWindow::isVisible() const {
    return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE;
}

bool DebugOverlayWindow::isExcludedFromCapture() const {
    DWORD affinity = 0;
    return hwnd_ != nullptr && GetWindowDisplayAffinity(hwnd_, &affinity) &&
           affinity == WDA_EXCLUDEFROMCAPTURE;
}

void DebugOverlayWindow::hide() {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void DebugOverlayWindow::ensureBitmap(core::SizeI size) {
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
        throwLastError("CreateDIBSection failed for the debug overlay");
    }
    bits_ = static_cast<std::uint32_t*>(bits);
    bitmapSize_ = size;
    previousBitmap_ = SelectObject(memoryDc_, bitmap_);
}

void DebugOverlayWindow::releaseBitmap() {
    if (bitmap_ == nullptr) {
        return;
    }
    SelectObject(memoryDc_, previousBitmap_);
    DeleteObject(bitmap_);
    bitmap_ = nullptr;
    bits_ = nullptr;
    bitmapSize_ = {};
}

core::RectI DebugOverlayWindow::drawStatusPanel(const core::DebugOverlay& overlay,
                                                core::SizeI size) {
    if (overlay.status.empty()) {
        return {};
    }
    const HGDIOBJ previousFont = SelectObject(memoryDc_, font_);
    SetBkMode(memoryDc_, TRANSPARENT);
    SetTextColor(memoryDc_, kPanelText);

    // 先量出面板要多大
    std::vector<std::wstring> rows;
    rows.reserve(overlay.status.size());
    int width = 0;
    int lineHeight = 0;
    for (const std::string& row : overlay.status) {
        rows.push_back(utf8ToWide(row));
        RECT measured{0, 0, 0, 0};
        DrawTextW(memoryDc_, rows.back().c_str(), -1, &measured, DT_CALCRECT | DT_SINGLELINE);
        width = std::max<int>(width, measured.right);
        lineHeight = std::max<int>(lineHeight, measured.bottom);
    }

    const int panelWidth = std::min(width + kPanelPadding * 2, size.width);
    const int panelHeight =
        std::min(lineHeight * static_cast<int>(rows.size()) + kPanelPadding * 2, size.height);
    RECT panel{0, 0, panelWidth, panelHeight};

    const HBRUSH background = CreateSolidBrush(kPanelBackground);
    FillRect(memoryDc_, &panel, background);
    DeleteObject(background);

    int y = kPanelPadding;
    for (const std::wstring& row : rows) {
        RECT where{kPanelPadding, y, panelWidth - kPanelPadding, y + lineHeight};
        DrawTextW(memoryDc_, row.c_str(), -1, &where, DT_SINGLELINE | DT_NOPREFIX);
        y += lineHeight;
    }

    SelectObject(memoryDc_, previousFont);
    return {0, 0, panelWidth, panelHeight};
}

void DebugOverlayWindow::update(const core::RectI& screenRect, const core::DebugOverlay& overlay) {
    const core::SizeI size{screenRect.width(), screenRect.height()};
    if (hwnd_ == nullptr || size.width <= 0 || size.height <= 0) {
        return;
    }
    ensureBitmap(size);

    const std::span<std::uint32_t> pixels(bits_,
                                          static_cast<std::size_t>(size.width) * size.height);
    core::renderDebugOverlayBoxes(overlay, size, pixels);

    // GDI 畫字不會設定 alpha，所以面板畫完之後整塊補成不透明；
    // 框是自己一個像素一個像素寫的，已經帶著 alpha。
    const core::RectI panel = drawStatusPanel(overlay, size);
    GdiFlush();
    for (int y = panel.top; y < panel.bottom; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * size.width;
        for (int x = panel.left; x < panel.right; ++x) {
            pixels[row + static_cast<std::size_t>(x)] |= 0xFF000000u;
        }
    }

    POINT source{0, 0};
    POINT destination{screenRect.left, screenRect.top};
    SIZE windowSize{size.width, size.height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, nullptr, &destination, &windowSize, memoryDc_, &source, 0, &blend,
                        ULW_ALPHA);
    if (!isVisible()) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }
}

}  // namespace tmw::platform
