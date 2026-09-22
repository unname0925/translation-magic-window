#include "support/test_window.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <stdexcept>

namespace tmw::test {
namespace {

constexpr wchar_t kClassName[] = L"TranslationMagicWindow.TestWindow";
constexpr UINT_PTR kAnimationTimer = 1;

void registerClassOnce() {
    static const bool registered = [] {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = [](HWND hwnd, UINT message, WPARAM wParam,
                                     LPARAM lParam) -> LRESULT {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        };
        windowClass.hInstance = GetModuleHandleW(nullptr);
        // 游標測試（IT-07）需要一個看得見的游標
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kClassName;
        return RegisterClassExW(&windowClass) != 0;
    }();
    if (!registered) {
        throw std::runtime_error("cannot register the test window class");
    }
}

}  // namespace

TestWindow::TestWindow(core::RectI screenRect) : TestWindow(screenRect, Options{}) {}

TestWindow::TestWindow(core::RectI screenRect, Options options)
    : rect_(screenRect), options_(options) {
    registerClassOnce();

    const int width = rect_.width();
    const int height = rect_.height();
    patternPixels_.resize(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto [b, g, r] = expectedPixel(x, y);
            patternPixels_[static_cast<size_t>(y) * width + x] =
                0xFF000000u | (static_cast<std::uint32_t>(r) << 16) |
                (static_cast<std::uint32_t>(g) << 8) | b;
        }
    }

    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"",
                            WS_POPUP, rect_.left, rect_.top, width, height, nullptr, nullptr,
                            GetModuleHandleW(nullptr), nullptr);
    if (hwnd_ == nullptr) {
        throw std::runtime_error("cannot create the test window");
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&TestWindow::windowProc));

    // Windows 11 會把視窗角落變圓，那幾個像素就不是我們畫的顏色了
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    if (options_.excludeFromCapture) {
        SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
    }
    if (options_.mode == Mode::Animated) {
        SetTimer(hwnd_, kAnimationTimer, 10, nullptr);
    }
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);
}

TestWindow::~TestWindow() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
}

void TestWindow::setSolidColor(COLORREF color) {
    options_.color = color;
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateWindow(hwnd_);
}

std::array<std::uint8_t, 3> TestWindow::expectedPixel(int x, int y) {
    return {static_cast<std::uint8_t>(x & 0xFF), static_cast<std::uint8_t>(y & 0xFF),
            static_cast<std::uint8_t>(((x >> 8) << 4) | (y >> 8))};
}

LRESULT CALLBACK TestWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<TestWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        switch (message) {
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                const HDC dc = BeginPaint(hwnd, &ps);
                self->paint(dc);
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_TIMER:
                if (wParam == kAnimationTimer) {
                    ++self->animationFrame_;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            case WM_MOUSEACTIVATE:
                return MA_NOACTIVATE;
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
                self->mouseEvents_.push_back(
                    {message, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}});
                return 0;
            case WM_NCDESTROY:
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                self->hwnd_ = nullptr;
                break;
            default:
                break;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void TestWindow::paint(HDC dc) {
    const RECT full{0, 0, rect_.width(), rect_.height()};
    if (options_.mode == Mode::Pattern) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = rect_.width();
        info.bmiHeader.biHeight = -rect_.height();
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        SetDIBitsToDevice(
            dc, 0, 0, static_cast<DWORD>(rect_.width()), static_cast<DWORD>(rect_.height()), 0, 0,
            0, static_cast<UINT>(rect_.height()), patternPixels_.data(), &info, DIB_RGB_COLORS);
        return;
    }
    if (options_.mode == Mode::Text) {
        const HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &full, white);
        DeleteObject(white);
        // 固定用 Segoe UI：每台機器的預設字型不同，OCR 的結果會跟著變
        const HFONT font = CreateFontW(
            -options_.fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        const HGDIOBJ previousFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(0, 0, 0));
        int y = options_.margin;
        for (const std::wstring& text : options_.lines) {
            TextOutW(dc, options_.margin, y, text.c_str(), static_cast<int>(text.size()));
            y += options_.fontHeight * 3 / 2;
        }
        SelectObject(dc, previousFont);
        DeleteObject(font);
        return;
    }
    COLORREF color = options_.color;
    if (options_.mode == Mode::Animated) {
        const unsigned f = animationFrame_;
        color = RGB((f * 7) & 0xFF, (f * 13) & 0xFF, (f * 29) & 0xFF);
    }
    const HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &full, brush);
    DeleteObject(brush);
}

int patternMismatches(const core::ImageBgra& image, int offsetX, int offsetY) {
    int mismatches = 0;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::uint8_t* p = image.pixel(x, y);
            const auto expected = TestWindow::expectedPixel(x + offsetX, y + offsetY);
            if (p[0] != expected[0] || p[1] != expected[1] || p[2] != expected[2]) {
                ++mismatches;
            }
        }
    }
    return mismatches;
}

void pumpMessages(std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (true) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return;
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(remaining), QS_ALLINPUT);
    }
}

bool waitUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        pumpMessages(std::chrono::milliseconds{10});
    }
    return true;
}

void waitForComposition() {
    pumpMessages(std::chrono::milliseconds{100});
    DwmFlush();
    pumpMessages(std::chrono::milliseconds{50});
}

}  // namespace tmw::test
