#include "platform/lens_window.h"

#include <windowsx.h>

#include <exception>
#include <span>
#include <utility>

#include "platform/win_error.h"

namespace tmw::platform {
namespace {

constexpr wchar_t kClassName[] = L"TranslationMagicWindow.Lens";

LRESULT toHitTestCode(core::LensHitZone zone) {
    switch (zone) {
        case core::LensHitZone::Move:
            return HTCAPTION;
        case core::LensHitZone::Left:
            return HTLEFT;
        case core::LensHitZone::Right:
            return HTRIGHT;
        case core::LensHitZone::Top:
            return HTTOP;
        case core::LensHitZone::Bottom:
            return HTBOTTOM;
        case core::LensHitZone::TopLeft:
            return HTTOPLEFT;
        case core::LensHitZone::TopRight:
            return HTTOPRIGHT;
        case core::LensHitZone::BottomLeft:
            return HTBOTTOMLEFT;
        case core::LensHitZone::BottomRight:
            return HTBOTTOMRIGHT;
        case core::LensHitZone::Transparent:
            break;
    }
    // 正常情況下不會走到這裡：alpha 為 0 的像素，系統根本不會送 WM_NCHITTEST 過來。
    return HTTRANSPARENT;
}

}  // namespace

LensWindow::LensWindow(HINSTANCE instance, core::SizeI contentSizeAt96Dpi, Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {
    registerWindowClass(instance);

    memoryDc_ = CreateCompatibleDC(nullptr);
    if (memoryDc_ == nullptr) {
        throwLastError("CreateCompatibleDC failed");
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    GetMonitorInfoW(monitor, &monitorInfo);
    const RECT work = monitorInfo.rcWork;

    // 建構子丟出例外時不會呼叫解構子，所以要自己釋放已經建立的資源
    try {
        // 先在目標螢幕上建立一個 1×1 的視窗，才能知道那個螢幕的 DPI
        CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                        kClassName, L"Translation Magic Window Lens", WS_POPUP, work.left, work.top,
                        1, 1, nullptr, nullptr, instance, this);
        if (hwnd_ == nullptr) {
            throwLastError("CreateWindowExW failed for the lens window");
        }

        // 排除擷取：透鏡不能出現在自己的擷取畫面中，否則 OCR 會讀到透鏡本身
        if (!SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE)) {
            throwLastError("SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed");
        }

        const unsigned windowDpi = dpi();
        const core::SizeI content{core::scaleForDpi(contentSizeAt96Dpi.width, windowDpi),
                                  core::scaleForDpi(contentSizeAt96Dpi.height, windowDpi)};
        const core::SizeI size = core::lensWindowSizeForContent(content, windowDpi);
        const int x = work.left + ((work.right - work.left) - size.width) / 2;
        const int y = work.top + ((work.bottom - work.top) - size.height) / 2;
        SetWindowPos(hwnd_, nullptr, x, y, size.width, size.height, SWP_NOZORDER | SWP_NOACTIVATE);
        render();
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    } catch (...) {
        destroy();
        throw;
    }
}

LensWindow::~LensWindow() {
    destroy();
}

void LensWindow::destroy() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
    releaseBitmap();
    if (memoryDc_ != nullptr) {
        DeleteDC(memoryDc_);
        memoryDc_ = nullptr;
    }
}

bool LensWindow::isVisible() const {
    return IsWindowVisible(hwnd_) != FALSE;
}

void LensWindow::setVisible(bool visible) {
    ShowWindow(hwnd_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void LensWindow::setAccent(core::Rgba accent) {
    if (accent == accent_) {
        return;
    }
    accent_ = accent;
    render();
}

core::RectI LensWindow::contentScreenRect() const {
    const core::RectI window = windowRect();
    return core::offset(currentLayout().content, window.left, window.top);
}

bool LensWindow::isExcludedFromCapture() const {
    DWORD affinity = 0;
    return GetWindowDisplayAffinity(hwnd_, &affinity) && affinity == WDA_EXCLUDEFROMCAPTURE;
}

void LensWindow::registerWindowClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    if (GetClassInfoExW(instance, kClassName, &windowClass)) {
        return;
    }
    windowClass.lpfnWndProc = &LensWindow::windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        throwLastError("RegisterClassExW failed for the lens window");
    }
}

LRESULT CALLBACK LensWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* self = static_cast<LensWindow*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<LensWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    // 例外不能穿過視窗程序傳回 Win32，否則行為未定義
    try {
        return self->handleMessage(message, wParam, lParam);
    } catch (const std::exception& error) {
        OutputDebugStringA(error.what());
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

LRESULT LensWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCHITTEST: {
            const core::RectI window = windowRect();
            const core::PointI point{GET_X_LPARAM(lParam) - window.left,
                                     GET_Y_LPARAM(lParam) - window.top};
            return toHitTestCode(core::hitTestLens(currentLayout(), point));
        }
        case WM_SETCURSOR:
            // 把手顯示「移動」游標；邊框的縮放游標由系統依照 WM_NCHITTEST 的結果處理
            if (LOWORD(lParam) == HTCAPTION) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                return TRUE;
            }
            break;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCLBUTTONDBLCLK:
            // 雙擊把手時不要最大化
            return 0;
        case WM_ENTERSIZEMOVE:
            if (callbacks_.onMoveSizeStart) {
                callbacks_.onMoveSizeStart();
            }
            return 0;
        case WM_EXITSIZEMOVE:
            if (callbacks_.onMoveSizeEnd) {
                callbacks_.onMoveSizeEnd();
            }
            return 0;
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                render();
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            const core::SizeI minimum = core::minimumLensWindowSize(dpi());
            info->ptMinTrackSize = {minimum.width, minimum.height};
            return 0;
        }
        case WM_DPICHANGED: {
            // 移到不同縮放比例的螢幕：採用系統建議的大小，邊框粗細也跟著重畫
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            render();
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

unsigned LensWindow::dpi() const {
    const UINT value = GetDpiForWindow(hwnd_);
    return value == 0 ? USER_DEFAULT_SCREEN_DPI : value;
}

core::RectI LensWindow::windowRect() const {
    RECT rect{};
    GetWindowRect(hwnd_, &rect);
    return {rect.left, rect.top, rect.right, rect.bottom};
}

core::LensLayout LensWindow::currentLayout() const {
    const core::RectI window = windowRect();
    return core::computeLensLayout({window.width(), window.height()}, dpi());
}

void LensWindow::ensureBitmap(core::SizeI size) {
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
        throwLastError("CreateDIBSection failed");
    }
    bits_ = static_cast<std::uint32_t*>(bits);
    bitmapSize_ = size;
    previousBitmap_ = SelectObject(memoryDc_, bitmap_);
}

void LensWindow::releaseBitmap() {
    if (bitmap_ == nullptr) {
        return;
    }
    SelectObject(memoryDc_, previousBitmap_);
    DeleteObject(bitmap_);
    bitmap_ = nullptr;
    bits_ = nullptr;
    bitmapSize_ = {};
}

void LensWindow::render() {
    if (hwnd_ == nullptr) {
        return;
    }
    const core::RectI window = windowRect();
    const core::SizeI size{window.width(), window.height()};
    if (size.width <= 0 || size.height <= 0) {
        return;
    }
    ensureBitmap(size);
    const core::LensLayout layout = core::computeLensLayout(size, dpi());
    core::renderLens(layout, accent_,
                     std::span(bits_, static_cast<size_t>(size.width) * size.height));

    POINT source{0, 0};
    POINT destination{window.left, window.top};
    SIZE windowSize{size.width, size.height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, nullptr, &destination, &windowSize, memoryDc_, &source, 0, &blend,
                        ULW_ALPHA);
}

}  // namespace tmw::platform
