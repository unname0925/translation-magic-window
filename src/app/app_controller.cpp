#include "app/app_controller.h"

#include <windowsx.h>

#include <exception>
#include <optional>
#include <string>
#include <utility>

#include "platform/app_paths.h"
#include "platform/png_file.h"
#include "platform/win_error.h"

namespace tmw::app {
namespace {

constexpr wchar_t kClassName[] = L"TranslationMagicWindow.Controller";
constexpr wchar_t kShowLensMessageName[] = L"TranslationMagicWindow.ShowLens";
constexpr UINT kTrayCallbackMessage = WM_APP + 1;

constexpr UINT kCommandToggleLens = 1;
constexpr UINT kCommandExit = 2;
constexpr UINT kCommandCapture = 3;
constexpr UINT kCommandOpenCaptures = 4;

constexpr int kHotkeyCapture = 1;
constexpr UINT_PTR kTimerRestoreAccent = 1;
constexpr UINT kFlashMilliseconds = 400;

// 擷取範圍的預設大小（96 DPI 基準）
constexpr core::SizeI kDefaultContentSize{480, 270};

// 邊框顏色。M0-07 會改由狀態機決定顏色。
constexpr core::Rgba kMovingAccent{245, 158, 11, 230};
constexpr core::Rgba kSuccessAccent{16, 185, 129, 230};
constexpr core::Rgba kErrorAccent{239, 68, 68, 230};

// capture-20260918-193012-123.png
std::wstring timestampedCaptureName() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[64]{};
    swprintf_s(name, L"capture-%04u%02u%02u-%02u%02u%02u-%03u.png", now.wYear, now.wMonth, now.wDay,
               now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    return name;
}

}  // namespace

AppController::AppController(HINSTANCE instance) {
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    showLensMessage_ = RegisterWindowMessageW(kShowLensMessageName);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &AppController::windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        platform::throwLastError("RegisterClassExW failed for the controller window");
    }

    // 不顯示的一般頂層視窗。不用 HWND_MESSAGE（純訊息視窗），
    // 因為系統匣選單需要擁有者能成為前景視窗，而且 FindWindow 也找不到純訊息視窗。
    CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"Translation Magic Window", WS_OVERLAPPED, 0, 0,
                    0, 0, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        platform::throwLastError("CreateWindowExW failed for the controller window");
    }

    try {
        capture_ = std::make_unique<platform::ScreenCapture>();

        platform::LensWindow::Callbacks callbacks;
        callbacks.onMoveSizeStart = [this] { lens_->setAccent(kMovingAccent); };
        callbacks.onMoveSizeEnd = [this] {
            lens_->setAccent(platform::LensWindow::kDefaultAccent);
        };
        lens_ = std::make_unique<platform::LensWindow>(instance, kDefaultContentSize,
                                                       std::move(callbacks));

        tray_ = std::make_unique<platform::TrayIcon>(hwnd_, kTrayCallbackMessage,
                                                     LoadIconW(nullptr, IDI_APPLICATION),
                                                     L"Translation Magic Window");

        // 快捷鍵被其他程式佔用時不算錯誤，系統匣選單一樣可以擷取
        captureHotkeyRegistered_ =
            RegisterHotKey(hwnd_, kHotkeyCapture, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,
                           'S') != FALSE;
    } catch (...) {
        DestroyWindow(hwnd_);
        throw;
    }
}

AppController::~AppController() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
}

int AppController::run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

void AppController::notifyRunningInstance() {
    const HWND running = FindWindowW(kClassName, nullptr);
    if (running != nullptr) {
        PostMessageW(running, RegisterWindowMessageW(kShowLensMessageName), 0, 0);
    }
}

LRESULT CALLBACK AppController::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* self = static_cast<AppController*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<AppController*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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

LRESULT AppController::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == kTrayCallbackMessage) {
        switch (LOWORD(lParam)) {
            case WM_CONTEXTMENU:
                showTrayMenu({GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
                break;
            case NIN_SELECT:
            case NIN_KEYSELECT:
                lens_->setVisible(!lens_->isVisible());
                break;
            default:
                break;
        }
        return 0;
    }
    if (message == taskbarCreatedMessage_ && tray_) {
        tray_->add();
        return 0;
    }
    if (message == showLensMessage_ && lens_) {
        lens_->setVisible(true);
        return 0;
    }

    switch (message) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kCommandToggleLens:
                    lens_->setVisible(!lens_->isVisible());
                    break;
                case kCommandCapture:
                    captureLensToFile();
                    break;
                case kCommandOpenCaptures:
                    ShellExecuteW(nullptr, L"open", platform::capturesDirectory().c_str(), nullptr,
                                  nullptr, SW_SHOWNORMAL);
                    break;
                case kCommandExit:
                    DestroyWindow(hwnd_);
                    break;
                default:
                    break;
            }
            return 0;
        case WM_HOTKEY:
            if (wParam == kHotkeyCapture) {
                captureLensToFile();
            }
            return 0;
        case WM_TIMER:
            if (wParam == kTimerRestoreAccent) {
                KillTimer(hwnd_, kTimerRestoreAccent);
                lens_->setAccent(platform::LensWindow::kDefaultAccent);
            }
            return 0;
        case WM_DISPLAYCHANGE:
            // 解析度或螢幕配置改變：下次擷取時重建工作階段
            capture_->reset();
            return 0;
        case WM_POWERBROADCAST:
            if (wParam == PBT_APMRESUMEAUTOMATIC) {
                capture_->reset();
            }
            return TRUE;
        case WM_DESTROY:
            if (captureHotkeyRegistered_) {
                UnregisterHotKey(hwnd_, kHotkeyCapture);
            }
            KillTimer(hwnd_, kTimerRestoreAccent);
            tray_.reset();
            lens_.reset();
            capture_.reset();
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void AppController::showTrayMenu(POINT anchor) {
    const HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    AppendMenuW(menu, MF_STRING | (lens_->isVisible() ? MF_CHECKED : MF_UNCHECKED),
                kCommandToggleLens, L"顯示透鏡");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandCapture,
                captureHotkeyRegistered_ ? L"擷取透鏡範圍\tCtrl+Alt+Shift+S" : L"擷取透鏡範圍");
    AppendMenuW(menu, MF_STRING, kCommandOpenCaptures, L"開啟擷取資料夾");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandExit, L"結束");

    // 擁有者必須是前景視窗，否則點選單外面時選單不會關閉（Windows 的已知行為）
    SetForegroundWindow(hwnd_);
    const UINT alignment = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | alignment, anchor.x, anchor.y, hwnd_,
                     nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void AppController::captureLensToFile() {
    if (!lens_->isVisible()) {
        return;
    }
    const std::optional<core::ImageBgra> image = capture_->readRegion(lens_->contentScreenRect());
    if (!image) {
        flashLens(kErrorAccent);
        return;
    }
    try {
        platform::savePng(*image, platform::capturesDirectory() / timestampedCaptureName());
        flashLens(kSuccessAccent);
    } catch (const std::exception& error) {
        OutputDebugStringA(error.what());
        flashLens(kErrorAccent);
    }
}

void AppController::flashLens(core::Rgba accent) {
    lens_->setAccent(accent);
    SetTimer(hwnd_, kTimerRestoreAccent, kFlashMilliseconds, nullptr);
}

}  // namespace tmw::app
