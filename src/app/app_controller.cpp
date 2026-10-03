#include "app/app_controller.h"

#include <windowsx.h>

#include <QApplication>
#include <QMessageBox>
#include <array>
#include <chrono>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "app/app_identity.h"
#include "app/translation_setup.h"
#include "core/debug_overlay.h"
#include "core/debug_report.h"
#include "core/hotkey.h"
#include "core/language.h"
#include "core/opencc_converter.h"
#include "core/overlay_font.h"
#include "net/cpr_http_client.h"
#include "ocr/lama_inpainter.h"
#include "ocr/onnx_model.h"
#include "platform/app_paths.h"
#include "platform/crash_dump.h"
#include "platform/debug_dump.h"
#include "platform/logging.h"
#include "platform/png_file.h"
#include "platform/secret.h"
#include "platform/settings_file.h"
#include "platform/text_encoding.h"
#include "platform/win_error.h"

namespace tmw::app {
namespace {

constexpr wchar_t kShowLensMessageName[] = L"TranslationMagicWindow.ShowLens";
constexpr UINT kTrayCallbackMessage = WM_APP + 1;

constexpr int kHotkeyCapture = 1;
constexpr int kHotkeyTranslate = 2;
constexpr int kHotkeyDebugDump = 3;
constexpr UINT_PTR kTimerRestoreAccent = 1;
constexpr UINT_PTR kTimerTick = 2;
constexpr UINT kFlashMilliseconds = 400;
constexpr UINT kTickMilliseconds = 100;

// 擷取範圍的預設大小（96 DPI 基準）
constexpr core::SizeI kDefaultContentSize{480, 270};
// 檢查新版本：啟動後等一下再檢查（整合測試每次都會啟動程式，不能每次都連 GitHub，
// 匿名的 API 每小時只有 60 次），之後每天一次
constexpr auto kFirstUpdateCheckDelay = std::chrono::minutes(2);
constexpr std::int64_t kUpdateCheckIntervalSeconds = 24 * 60 * 60;
// 新增的透鏡往右下錯開多少（實體像素），免得和前一個疊在一起看不出來
constexpr int kNewLensOffset = 48;

// 邊框顏色（見 docs/design.md 4.1 的「狀態顯示」）
constexpr core::Rgba kDraggingAccent{245, 158, 11, 230};    // 橘：拖動中
constexpr core::Rgba kSettlingAccent{250, 204, 21, 230};    // 黃：等待畫面穩定
constexpr core::Rgba kProcessingAccent{139, 92, 246, 230};  // 紫：處理中
constexpr core::Rgba kSuccessAccent{16, 185, 129, 230};     // 綠：手動擷取成功
constexpr core::Rgba kErrorAccent{239, 68, 68, 230};        // 紅：失敗

core::Rgba accentFor(core::LensState state) {
    switch (state) {
        case core::LensState::Dragging:
            return kDraggingAccent;
        case core::LensState::Settling:
            return kSettlingAccent;
        case core::LensState::Processing:
            return kProcessingAccent;
        case core::LensState::Showing:
            break;
    }
    return platform::LensWindow::kDefaultAccent;  // 藍：顯示中（平常的狀態）
}

// capture-20260918-193012-123.png
std::wstring timestampedCaptureName() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[64]{};
    swprintf_s(name, L"capture-%04u%02u%02u-%02u%02u%02u-%03u.png", now.wYear, now.wMonth, now.wDay,
               now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    return name;
}

const char* stateName(core::LensState state) {
    switch (state) {
        case core::LensState::Dragging:
            return "拖動中";
        case core::LensState::Settling:
            return "等待畫面穩定";
        case core::LensState::Processing:
            return "處理中";
        case core::LensState::Showing:
            break;
    }
    return "顯示中";
}

// 「2026-09-22 13:45:01」，除錯傾印的報告裡用
std::string localTimeText() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char text[32]{};
    sprintf_s(text, "%04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour,
              now.wMinute, now.wSecond);
    return text;
}

std::wstring timestampedDumpFolderName() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[64]{};
    swprintf_s(name, L"debug-%04u%02u%02u-%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour,
               now.wMinute, now.wSecond);
    return name;
}

// 設定檔的快捷鍵寫法轉成 RegisterHotKey 的參數（M2-10）。看不懂時回傳 false。
bool toWin32Hotkey(const std::string& text, UINT& modifiers, UINT& virtualKey) {
    const std::optional<core::Hotkey> hotkey = core::parseHotkey(text);
    if (!hotkey) {
        return false;
    }
    modifiers = MOD_NOREPEAT;
    modifiers |= hotkey->ctrl ? MOD_CONTROL : 0;
    modifiers |= hotkey->alt ? MOD_ALT : 0;
    modifiers |= hotkey->shift ? MOD_SHIFT : 0;
    modifiers |= hotkey->win ? MOD_WIN : 0;
    if (hotkey->key.size() == 1) {
        virtualKey = static_cast<UINT>(hotkey->key[0]);  // A～Z、0～9 的虛擬鍵碼就是 ASCII
    } else {
        virtualKey = VK_F1 + static_cast<UINT>(std::stoi(hotkey->key.substr(1)) - 1);
    }
    return true;
}

// 系統匣選單的項目：快捷鍵有註冊成功才在右邊顯示按鍵
std::wstring menuLabel(const wchar_t* label, bool registered, const std::string& hotkey) {
    std::wstring out = label;
    if (registered) {
        const std::optional<core::Hotkey> parsed = core::parseHotkey(hotkey);
        out += L"\t" + platform::utf8ToWide(parsed ? core::formatHotkey(*parsed) : hotkey);
    }
    return out;
}

// 系統匣「辨識語言」子選單的每一項，和它在設定檔裡的值
struct LanguageMenuItem {
    UINT command;
    const char* code;
    const wchar_t* label;
};
constexpr std::array<LanguageMenuItem, 4> kLanguageMenu{{
    {kCommandLanguageAuto, "auto", L"自動判斷"},
    {kCommandLanguageJapanese, "ja", L"日文"},
    {kCommandLanguageEnglish, "en", L"英文"},
    {kCommandLanguageKorean, "ko", L"韓文"},
}};

// 系統匣「情境」子選單的每一項（M2-06）
struct ProfileMenuItem {
    UINT command;
    const char* id;
    const wchar_t* label;
};
constexpr std::array<ProfileMenuItem, 4> kProfileMenu{{
    {kCommandProfileNone, "", L"不使用"},
    {kCommandProfileManga, "manga", L"漫畫"},
    {kCommandProfileGame, "game", L"遊戲"},
    {kCommandProfileWeb, "web", L"網頁"},
}};

std::string profileForCommand(UINT command) {
    for (const ProfileMenuItem& item : kProfileMenu) {
        if (item.command == command) {
            return item.id;
        }
    }
    return "";
}

std::string ocrLanguageForCommand(UINT command) {
    for (const LanguageMenuItem& item : kLanguageMenu) {
        if (item.command == command) {
            return item.code;
        }
    }
    return "auto";
}

}  // namespace

AppController::AppController(HINSTANCE instance, std::filesystem::path dataDirectory,
                             std::filesystem::path settingsPath, core::Settings settings,
                             ocr::Device ocrDevice)
    : dataDirectory_(std::move(dataDirectory)),
      settingsPath_(std::move(settingsPath)),
      settings_(std::move(settings)),
      ocrDevice_(ocrDevice) {
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    showLensMessage_ = RegisterWindowMessageW(kShowLensMessageName);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &AppController::windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kControllerClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        platform::throwLastError("RegisterClassExW failed for the controller window");
    }

    // 不顯示的一般頂層視窗。不用 HWND_MESSAGE（純訊息視窗），
    // 因為系統匣選單需要擁有者能成為前景視窗，而且 FindWindow 也找不到純訊息視窗。
    CreateWindowExW(WS_EX_TOOLWINDOW, kControllerClassName, L"Translation Magic Window",
                    WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        platform::throwLastError("CreateWindowExW failed for the controller window");
    }

    try {
        capture_ = std::make_unique<platform::ScreenCapture>();
        frameSource_ = std::make_unique<platform::CaptureFrameSource>(*capture_);

        // 上次結束時的透鏡（位置、大小、個數）；第一次啟動是一個，在螢幕中央
        if (settings_.lenses.empty()) {
            addLens();
        }
        for (const core::RectI& placement : settings_.lenses) {
            addLens(&placement);
        }

        tray_ = std::make_unique<platform::TrayIcon>(hwnd_, kTrayCallbackMessage,
                                                     LoadIconW(nullptr, IDI_APPLICATION),
                                                     L"Translation Magic Window");

        // 快捷鍵被其他程式佔用時不算錯誤，系統匣選單一樣可以操作
        for (const std::string& failed : registerHotkeys()) {
            platform::logWarn("快捷鍵 " + failed + " 已經被其他程式佔用，請到設定裡換一組");
        }

        setUpPipeline();
        setOverlayEnabled(settings_.overlay);

        resultWindow_ = std::make_unique<ui::ResultWindow>();
        resultWindow_->restoreGeometry(settings_.resultWindow.geometry);
        resultWindow_->setFontPointSize(settings_.resultWindow.fontPoints);
        resultWindow_->setAlwaysOnTop(settings_.resultWindow.alwaysOnTop);
        QObject::connect(resultWindow_.get(), &ui::ResultWindow::settingsChanged,
                         [this] { saveSettings(); });

        SetTimer(hwnd_, kTimerTick, kTickMilliseconds, nullptr);
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
    // Qt 的事件迴圈同時會處理 Win32 的訊息（透鏡視窗和這個主控視窗都靠它），
    // 所以不需要自己寫訊息迴圈（design.md 3.3：UI 執行緒同時跑兩者）。
    return QApplication::exec();
}

void AppController::notifyRunningInstance() {
    const HWND running = FindWindowW(kControllerClassName, nullptr);
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
                setLensVisible(!lenses_.front()->window->isVisible());
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
    if (message == showLensMessage_ && !lenses_.empty()) {
        setLensVisible(true);
        return 0;
    }

    switch (message) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kCommandToggleLens:
                    setLensVisible(!lenses_.front()->window->isVisible());
                    break;
                case kCommandAddLens:
                    addLens();
                    break;
                case kCommandOpenUpdate:
                    if (availableUpdate_) {
                        ShellExecuteW(nullptr, L"open",
                                      platform::utf8ToWide(availableUpdate_->url).c_str(), nullptr,
                                      nullptr, SW_SHOWNORMAL);
                    }
                    break;
                case kCommandRemoveLens:
                    removeLens();
                    break;
                case kCommandCapture:
                    if (Lens& lens = activeLens(); lens.window->isVisible()) {
                        flashLens(lens, saveLensCapture(lens) ? kSuccessAccent : kErrorAccent);
                    }
                    break;
                case kCommandOpenCaptures:
                    ShellExecuteW(nullptr, L"open",
                                  platform::capturesDirectory(dataDirectory_).c_str(), nullptr,
                                  nullptr, SW_SHOWNORMAL);
                    break;
                case kCommandToggleAutoSave:
                    autoSave_ = !autoSave_;
                    break;
                case kCommandOpenResults:
                    showResultWindow();
                    break;
                case kCommandSettings:
                    openSettings();
                    break;
                case kCommandDebugDump:
                    flashLens(activeLens(),
                              writeDebugDump().empty() ? kErrorAccent : kSuccessAccent);
                    break;
                case kCommandToggleMangaMode:
                    setMangaMode(!settings_.mangaMode);
                    saveSettings();
                    break;
                case kCommandProfileNone:
                case kCommandProfileManga:
                case kCommandProfileGame:
                case kCommandProfileWeb:
                    setProfile(profileForCommand(LOWORD(wParam)));
                    break;
                case kCommandToggleGameMode:
                    setGameMode(!settings_.gameMode);
                    saveSettings();
                    break;
                case kCommandToggleOverlay:
                    setOverlayEnabled(!settings_.overlay);
                    saveSettings();
                    break;
                case kCommandEditGlossary:
                    openGlossary();
                    break;
                case kCommandLanguageAuto:
                case kCommandLanguageJapanese:
                case kCommandLanguageEnglish:
                case kCommandLanguageKorean:
                    setOcrLanguage(ocrLanguageForCommand(LOWORD(wParam)));
                    saveSettings();
                    break;
                case kCommandToggleDebugOverlay:
                    setDebugOverlayEnabled(!debugOverlayEnabled_);
                    break;
                case kCommandTogglePause:
                    setPaused(!paused_);
                    break;
                case kCommandTranslateNow:
                    translateNow();
                    break;
                case kCommandExit:
                    DestroyWindow(hwnd_);
                    break;
                default:
                    break;
            }
            return 0;
        case WM_HOTKEY:
            if (Lens& lens = activeLens(); wParam == kHotkeyCapture && lens.window->isVisible()) {
                flashLens(lens, saveLensCapture(lens) ? kSuccessAccent : kErrorAccent);
            } else if (wParam == kHotkeyTranslate) {
                translateNow();
            } else if (wParam == kHotkeyDebugDump) {
                flashLens(lens, writeDebugDump().empty() ? kErrorAccent : kSuccessAccent);
            }
            return 0;
        case WM_TIMER:
            if (wParam == kTimerTick) {
                maybeCheckForUpdates();
                for (const std::unique_ptr<Lens>& lens : lenses_) {
                    lens->trigger->tick();
                    refreshDebugOverlay(*lens);
                    refreshOverlay(*lens);
                }
            } else if (wParam == kTimerRestoreAccent) {
                KillTimer(hwnd_, kTimerRestoreAccent);
                for (const std::unique_ptr<Lens>& lens : lenses_) {
                    lens->flashing = false;
                    updateAccent(*lens);
                }
            }
            return 0;
        case WM_DISPLAYCHANGE:
            // 解析度或螢幕配置改變：重建擷取，並重新等待畫面穩定
            capture_->reset();
            for (const std::unique_ptr<Lens>& lens : lenses_) {
                lens->trigger->onMoveSizeEnd(lens->window->contentScreenRect());
            }
            return 0;
        case WM_POWERBROADCAST:
            if (wParam == PBT_APMRESUMEAUTOMATIC) {
                capture_->reset();
                for (const std::unique_ptr<Lens>& lens : lenses_) {
                    lens->trigger->onMoveSizeEnd(lens->window->contentScreenRect());
                }
            }
            return TRUE;
        case WM_DESTROY:
            KillTimer(hwnd_, kTimerTick);
            KillTimer(hwnd_, kTimerRestoreAccent);
            unregisterHotkeys();
            // 記住每個透鏡的位置，下次啟動照樣開回來
            settings_.lenses.clear();
            for (const std::unique_ptr<Lens>& lens : lenses_) {
                settings_.lenses.push_back(lens->window->windowRect());
            }
            saveSettings();
            // 依相依關係的反向順序釋放：透鏡（視窗和觸發器）會用到 frameSource_
            lenses_.clear();
            tray_.reset();
            frameSource_.reset();
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
    if (availableUpdate_) {
        const std::wstring label =
            L"下載新版本 " + platform::utf8ToWide(availableUpdate_->version) + L"…";
        AppendMenuW(menu, MF_STRING, kCommandOpenUpdate, label.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu,
                MF_STRING | (lenses_.front()->window->isVisible() ? MF_CHECKED : MF_UNCHECKED),
                kCommandToggleLens, L"顯示透鏡");
    const std::wstring addLabel = L"新增透鏡（目前 " + std::to_wstring(lenses_.size()) + L" 個）";
    AppendMenuW(menu, MF_STRING | (lenses_.size() < kMaxLenses ? MF_ENABLED : MF_GRAYED),
                kCommandAddLens, addLabel.c_str());
    AppendMenuW(menu, MF_STRING | (lenses_.size() > 1 ? MF_ENABLED : MF_GRAYED), kCommandRemoveLens,
                L"關閉最後新增的透鏡");
    AppendMenuW(menu, MF_STRING | (settings_.overlay ? MF_CHECKED : MF_UNCHECKED),
                kCommandToggleOverlay, L"在原位顯示譯文");
    AppendMenuW(menu, MF_STRING, kCommandOpenResults, L"開啟結果視窗");
    AppendMenuW(
        menu, MF_STRING, kCommandTranslateNow,
        menuLabel(L"立即翻譯", translateHotkeyRegistered_, settings_.hotkeys.translate).c_str());
    AppendMenuW(menu, MF_STRING | (paused_ ? MF_CHECKED : MF_UNCHECKED), kCommandTogglePause,
                L"暫停");
    AppendMenuW(menu, MF_STRING | (settings_.mangaMode ? MF_CHECKED : MF_UNCHECKED),
                kCommandToggleMangaMode, L"漫畫模式（依對話框分段）");
    AppendMenuW(menu, MF_STRING | (settings_.gameMode ? MF_CHECKED : MF_UNCHECKED),
                kCommandToggleGameMode, L"遊戲模式（只看文字區域）");
    if (const HMENU profiles = CreatePopupMenu(); profiles != nullptr) {
        for (const ProfileMenuItem& item : kProfileMenu) {
            AppendMenuW(profiles, MF_STRING, item.command, item.label);
            if (settings_.profile == item.id) {
                CheckMenuRadioItem(profiles, kProfileMenu.front().command,
                                   kProfileMenu.back().command, item.command, MF_BYCOMMAND);
            }
        }
        // 子選單交給父選單管理，DestroyMenu(menu) 時一起釋放
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(profiles), L"情境");
    }
    if (const HMENU languages = CreatePopupMenu(); languages != nullptr) {
        for (const LanguageMenuItem& item : kLanguageMenu) {
            AppendMenuW(languages, MF_STRING, item.command, item.label);
            if (settings_.ocrLanguage == item.code) {
                CheckMenuRadioItem(languages, kLanguageMenu.front().command,
                                   kLanguageMenu.back().command, item.command, MF_BYCOMMAND);
            }
        }
        // 子選單交給父選單管理，DestroyMenu(menu) 時一起釋放
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(languages), L"辨識語言");
    }
    AppendMenuW(menu, MF_STRING, kCommandEditGlossary, L"編輯專有名詞表…");
    AppendMenuW(menu, MF_STRING, kCommandSettings, L"設定…");
    AppendMenuW(
        menu, MF_STRING, kCommandDebugDump,
        menuLabel(L"除錯傾印", debugDumpHotkeyRegistered_, settings_.hotkeys.debugDump).c_str());
    AppendMenuW(menu, MF_STRING | (debugOverlayEnabled_ ? MF_CHECKED : MF_UNCHECKED),
                kCommandToggleDebugOverlay, L"除錯覆蓋框（顯示 OCR 框和耗時）");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (autoSave_ ? MF_CHECKED : MF_UNCHECKED), kCommandToggleAutoSave,
                L"畫面穩定後自動存成 PNG（測試用）");
    AppendMenuW(
        menu, MF_STRING, kCommandCapture,
        menuLabel(L"擷取透鏡範圍", captureHotkeyRegistered_, settings_.hotkeys.capture).c_str());
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

void AppController::setLensVisible(bool visible) {
    for (const std::unique_ptr<Lens>& lens : lenses_) {
        lens->window->setVisible(visible);
        // 隱藏時完全不取樣、不觸發；重新顯示時從「等待穩定」開始
        lens->trigger->setEnabled(visible && !paused_);
    }
}

void AppController::addLens(const core::RectI* placement) {
    if (lenses_.size() >= kMaxLenses) {
        return;
    }
    auto lens = std::make_unique<Lens>();
    lens->id = nextLensId_++;
    Lens* const self = lens.get();  // 放在堆積上，加進 lenses_ 之後位址不變

    core::AutoTrigger::Callbacks triggerCallbacks;
    triggerCallbacks.onStateChanged = [this, self](core::LensState) { updateAccent(*self); };
    triggerCallbacks.onProcess = [this, self](const core::ProcessRequest& request) {
        process(*self, request);
    };
    triggerCallbacks.onPrepare = [this, self](std::uint64_t ticket) { prepare(*self, ticket); };
    core::AutoTriggerConfig triggerConfig;
    triggerConfig.focusOnText = settings_.gameMode;
    triggerConfig.settleTime = std::chrono::milliseconds(settings_.settleMs);
    lens->trigger = std::make_unique<core::AutoTrigger>(clock_, *frameSource_, triggerConfig,
                                                        std::move(triggerCallbacks));

    platform::LensWindow::Callbacks lensCallbacks;
    lensCallbacks.onMoveSizeStart = [this, self] {
        activeLensId_ = self->id;  // 擷取、除錯傾印跟著最後拖動的透鏡
        self->trigger->onMoveSizeStart();
    };
    lensCallbacks.onMoveSizeEnd = [self] {
        self->trigger->onMoveSizeEnd(self->window->contentScreenRect());
    };
    lens->window = std::make_unique<platform::LensWindow>(
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE)), kDefaultContentSize,
        std::move(lensCallbacks));
    if (placement != nullptr && lens->window->setWindowRect(*placement)) {
        // 放回上次的位置
    } else if (!lenses_.empty()) {
        // 新的透鏡和第一個一樣出現在螢幕中央，疊在一起看不出來：往右下錯開
        const int step = kNewLensOffset * static_cast<int>(lenses_.size());
        lens->window->moveBy(step, step);
    }
    lens->trigger->setEnabled(!paused_);
    lens->trigger->onMoveSizeEnd(lens->window->contentScreenRect());
    updateAccent(*lens);
    attachOverlays(*lens);
    activeLensId_ = lens->id;
    platform::logInfo("新增透鏡 " + std::to_string(lens->id) + "，共 " +
                      std::to_string(lenses_.size() + 1) + " 個");
    lenses_.push_back(std::move(lens));
}

void AppController::removeLens() {
    if (lenses_.size() <= 1) {
        return;  // 第一個透鏡不關，只能藏起來
    }
    const int id = lenses_.back()->id;
    if (worker_ != nullptr) {
        worker_->cancel(id);  // 還在路上的結果回來時找不到透鏡，會被丟掉
    }
    lenses_.pop_back();
    if (activeLensId_ == id) {
        activeLensId_ = lenses_.front()->id;
    }
    platform::logInfo("關閉透鏡 " + std::to_string(id) + "，剩 " + std::to_string(lenses_.size()) +
                      " 個");
}

AppController::Lens* AppController::findLens(int id) {
    for (const std::unique_ptr<Lens>& lens : lenses_) {
        if (lens->id == id) {
            return lens.get();
        }
    }
    return nullptr;
}

AppController::Lens& AppController::activeLens() {
    Lens* lens = findLens(activeLensId_);
    return lens != nullptr ? *lens : *lenses_.front();
}

void AppController::translateNow() {
    if (paused_) {
        return;
    }
    for (const std::unique_ptr<Lens>& lens : lenses_) {
        if (lens->window->isVisible()) {
            lens->trigger->manualTrigger();
        }
    }
}

void AppController::process(Lens& lens, const core::ProcessRequest& request) {
    if (autoSave_ && !saveLensCapture(lens)) {
        flashLens(lens, kErrorAccent);
    }
    const platform::LogContext context{.lens = lens.id, .sequence = request.generation};
    if (worker_ == nullptr || !lens.window->isVisible()) {
        platform::log(platform::LogLevel::Info, context,
                      worker_ == nullptr ? "沒有可用的處理管線，跳過" : "透鏡沒顯示，跳過");
        lens.trigger->onProcessingFinished(request.generation);
        return;
    }
    const core::RectI region = lens.window->contentScreenRect();
    std::optional<core::ImageBgra> frame = capture_->readRegion(region);
    if (!frame) {
        platform::log(platform::LogLevel::Warn, context, "擷取透鏡範圍失敗，這次不處理");
        lens.trigger->onProcessingFinished(request.generation);
        return;
    }
    core::PipelineJob job;
    job.generation = request.generation;
    job.lens = lens.id;
    job.region = region;
    job.frame = std::move(*frame);
    job.manual = request.manual;
    job.usePrepared = request.prepared;
    job.language = settings_.ocrLanguage;
    job.glossary = currentGlossary();
    if (lens.overlay != nullptr) {
        job.inpainter = inpainter_;  // 只有譯文要蓋在原文上時才修補背景
    }
    platform::log(platform::LogLevel::Info, context,
                  std::string(request.manual ? "手動" : "自動") + "觸發，開始處理");
    worker_->submit(std::move(job));
}

void AppController::prepare(Lens& lens, std::uint64_t ticket) {
    // 畫面停下一下就先做 OCR（速度優化 4）：失敗或沒有管線就算了，到時候正式處理照常做
    if (worker_ == nullptr || !lens.window->isVisible()) {
        return;
    }
    const core::RectI region = lens.window->contentScreenRect();
    std::optional<core::ImageBgra> frame = capture_->readRegion(region);
    if (!frame) {
        return;
    }
    core::PipelineJob job;
    job.lens = lens.id;
    job.region = region;
    job.frame = std::move(*frame);
    job.language = settings_.ocrLanguage;
    job.prepareTicket = ticket;
    worker_->submit(std::move(job));
}

void AppController::setUpPipeline() {
    const std::filesystem::path models = platform::findModelsDirectory();
    if (models.empty()) {
        platform::logWarn(
            "找不到 OCR 模型的資料夾，這次執行不會翻譯"
            "（請把 models 放在執行檔旁邊，或執行 tools/fetch_models）");
        return;
    }
    try {
        // 固定偵測的輸入大小。透鏡可以調整大小，但形狀一變，DirectML 上那個大小的
        // 每一次推論都會永久慢 3～5 倍，暖機也白做了（M1-03；M1-15 量出整張漫畫頁
        // 371 ms → 189 ms，偵測 100 ms → 20 ms）。
        ocr::OcrOptions ocrOptions;
        ocrOptions.detection.fixedInput = ocr::lensDetectionInput();
        // 只暖機會用到的那個辨識模型：指定韓文的人不必付主模型第一次推論的記憶體
        ocrOptions.warmUpScript = core::languageFromCode(settings_.ocrLanguage);
        // 主模型和韓文模型都載入，語言自動判斷或由設定指定（design.md 4.4）。
        // 只載日文模型的話，韓文畫面讀出來的是一整頁空字串。
        // 載入但沒推論過的模型只佔 30～40 MB，所以兩個都先載入，切換語言時不必等。
        ocr_ = std::make_unique<ocr::OcrService>(models, ocrDevice_, ocrOptions);
    } catch (const std::exception& error) {
        platform::logError(std::string("OCR 模型載入失敗，這次執行不會翻譯：") + error.what());
        return;
    }
    platform::logInfo(std::string("OCR 裝置：") + std::string(ocr::deviceName(ocr_->device())));
    if (const std::filesystem::path directML = ocr::loadedDirectMLPath(); !directML.empty()) {
        // 預設用 Windows 內建的那一份（System32）；程式資料夾裡有附的話會先用附的
        platform::logInfo("DirectML：" + platform::pathToUtf8(directML));
    }
    // ルビ的一般讀音表（M2-13，tools/eval/furigana_dict.py build 產生）。沒有就用片假名規則
    {
        std::ifstream file(models / L"furigana" / L"readings.tsv", std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
        if (std::optional<core::FuriganaReadings> readings = core::FuriganaReadings::parse(text)) {
            platform::logInfo("ルビ讀音表：" + std::to_string(readings->kanjiCount()) + " 個漢字");
            furigana_ = std::make_shared<const core::FuriganaReadings>(std::move(*readings));
        } else {
            platform::logInfo("沒有ルビ讀音表，特殊讀音改用「讀音是片假名」判斷");
        }
    }
    if (settings_.mangaMode) {
        setMangaMode(true);
    }
    setOcrLanguage(settings_.ocrLanguage);  // 記下目前的辨識語言
    rebuildTranslation();
}

std::shared_ptr<const core::Glossary> AppController::currentGlossary() {
    const std::filesystem::path path = dataDirectory_ / L"glossary.txt";
    std::error_code error;
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(path, error);
    if (error) {
        glossary_.reset();  // 沒有檔案（或被刪掉了）
        glossaryTime_ = {};
        return nullptr;
    }
    if (glossary_ != nullptr && written == glossaryTime_) {
        return glossary_;  // 沒改過：不必每次處理都讀檔
    }
    std::ifstream file(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    core::GlossaryLoad load = core::parseGlossary(text);
    for (const std::string& problem : load.problems) {
        platform::logWarn("專有名詞表 " + problem + "，這一行略過");
    }
    platform::logInfo("專有名詞表：" + std::to_string(load.entries.size()) + " 個詞");
    glossary_ = std::make_shared<const core::Glossary>(std::move(load.entries));
    glossaryTime_ = written;
    return glossary_;
}

void AppController::openGlossary() {
    const std::filesystem::path path = dataDirectory_ / L"glossary.txt";
    if (!std::filesystem::exists(path)) {
        std::ofstream file(path, std::ios::binary);
        file << "\xEF\xBB\xBF" << core::glossaryTemplate();  // 有 BOM，記事本才不會猜錯編碼
    }
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::vector<std::string> AppController::registerHotkeys() {
    struct Entry {
        int id;
        const std::string* text;
        bool* registered;
        const char* name;
    };
    const std::array<Entry, 3> entries{{
        {kHotkeyTranslate, &settings_.hotkeys.translate, &translateHotkeyRegistered_, "立即翻譯"},
        {kHotkeyDebugDump, &settings_.hotkeys.debugDump, &debugDumpHotkeyRegistered_, "除錯傾印"},
        {kHotkeyCapture, &settings_.hotkeys.capture, &captureHotkeyRegistered_, "擷取透鏡範圍"},
    }};
    std::vector<std::string> failed;
    for (const Entry& entry : entries) {
        *entry.registered = false;
        if (entry.text->empty()) {
            continue;  // 不使用這個快捷鍵
        }
        UINT modifiers = 0;
        UINT key = 0;
        if (toWin32Hotkey(*entry.text, modifiers, key) &&
            RegisterHotKey(hwnd_, entry.id, modifiers, key) != FALSE) {
            *entry.registered = true;
        } else {
            failed.push_back(*entry.text + "（" + entry.name + "）");
        }
    }
    return failed;
}

void AppController::unregisterHotkeys() {
    if (captureHotkeyRegistered_) {
        UnregisterHotKey(hwnd_, kHotkeyCapture);
    }
    if (translateHotkeyRegistered_) {
        UnregisterHotKey(hwnd_, kHotkeyTranslate);
    }
    if (debugDumpHotkeyRegistered_) {
        UnregisterHotKey(hwnd_, kHotkeyDebugDump);
    }
    captureHotkeyRegistered_ = translateHotkeyRegistered_ = debugDumpHotkeyRegistered_ = false;
}

void AppController::setProfile(const std::string& id) {
    core::switchProfile(settings_, id);
    applyProfileValues();
    saveSettings();
    platform::logInfo("情境：" + (id.empty() ? std::string("不使用") : core::profileName(id)));
}

void AppController::applyProfileValues() {
    // 情境切換或設定視窗存檔之後，把現在的值一次套用到各個地方
    setMangaMode(settings_.mangaMode);  // 模型載入失敗時它會把 mangaMode 留在 false
    setGameMode(settings_.gameMode);
    setOcrLanguage(settings_.ocrLanguage);
    for (const std::unique_ptr<Lens>& lens : lenses_) {
        lens->trigger->setSettleTime(std::chrono::milliseconds(settings_.settleMs));
    }
    rebuildTranslation();  // 處理管線的「碰到邊緣的句子不翻」
}

void AppController::setGameMode(bool enabled) {
    settings_.gameMode = enabled;
    for (const std::unique_ptr<Lens>& lens : lenses_) {
        lens->trigger->setFocusOnText(enabled);
    }
    platform::logInfo(enabled ? "遊戲模式：開（辨識之後只看文字區域有沒有變）" : "遊戲模式：關");
}

void AppController::setOcrLanguage(const std::string& code) {
    const core::Language language = core::languageFromCode(code);
    settings_.ocrLanguage = language == core::Language::Unknown ? "auto" : code;
    for (const LanguageMenuItem& item : kLanguageMenu) {
        if (settings_.ocrLanguage == item.code) {
            platform::logInfo("辨識語言：" + platform::wideToUtf8(item.label));
        }
    }
}

void AppController::setMangaMode(bool enabled) {
    if (ocr_ == nullptr) {
        settings_.mangaMode = false;
        return;
    }
    if (!ocr_->setMangaMode(enabled)) {
        platform::logWarn("漫畫模式打不開：找不到或載入不了 " +
                          platform::pathToUtf8(ocr::OcrService::comicTextModelPath(
                              platform::findModelsDirectory())) +
                          "（執行 tools/fetch_models 下載 comic-text-detector）");
        settings_.mangaMode = false;
        return;
    }
    settings_.mangaMode = enabled;
    if (!enabled) {
        platform::logInfo("漫畫模式：關");
    } else if (ocr_->hasMangaOcr()) {
        platform::logInfo("漫畫模式：開（依對話框分段，直排對白用 manga-ocr 重讀）");
    } else {
        // 分段照樣有效，只是文字還是 PP-OCR 讀的（字元錯誤率 10.4% 對 5.0%）
        platform::logWarn("漫畫模式：開，但找不到 " +
                          platform::pathToUtf8(
                              ocr::OcrService::mangaOcrDirectory(platform::findModelsDirectory())) +
                          "，直排對白仍由 PP-OCR 辨識（執行 tools/eval/export_manga_decoder.py "
                          "--install 產生）");
    }
}

void AppController::rebuildTranslation() {
    // 解構的順序很重要：工作執行緒握著 pipeline_，pipeline_ 握著 translation_
    worker_.reset();
    pipeline_.reset();
    translation_.reset();
    if (ocr_ == nullptr) {
        return;
    }

    const std::filesystem::path opencc =
        core::OpenccConverter::defaultConfig(platform::executableDirectory() / L"opencc");
    TranslationSetup setup = makeTranslationService(settings_, clock_, opencc);
    for (const std::string& problem : setup.problems) {
        platform::logWarn(problem);
    }
    translation_ = std::move(setup.service);
    std::string engines;
    for (const std::string& id : setup.engineIds) {
        engines += engines.empty() ? "" : " → ";
        engines += id;
    }
    platform::logInfo("翻譯引擎鏈：" +
                      (engines.empty() ? std::string("（沒有可用的引擎）") : engines));

    core::PipelineOptions pipelineOptions;
    pipelineOptions.dropEdgeBlocks = settings_.dropEdgeBlocks;
    pipelineOptions.furigana = furigana_;
    pipeline_ = std::make_unique<core::Pipeline>(*ocr_, *translation_, pipelineOptions);
    worker_ =
        std::make_unique<core::PipelineWorker>(*pipeline_, [this](core::PipelineResult result) {
            // 這裡是工作執行緒。回到 UI 執行緒才能碰透鏡和結果視窗（design.md 3.3）。
            QMetaObject::invokeMethod(
                QApplication::instance(),
                [this, moved = std::move(result)] { onPipelineResult(moved); },
                Qt::QueuedConnection);
        });
    setUpWeb();
}

void AppController::setUpWeb() {
    WebService::Callbacks callbacks;
    callbacks.post = [](std::function<void()> work) {
        QMetaObject::invokeMethod(QApplication::instance(), std::move(work), Qt::QueuedConnection);
    };
    callbacks.configure = [this](core::PipelineJob& job) {
        job.language = settings_.ocrLanguage;
        job.glossary = currentGlossary();
        job.inpainter = inpainter_;  // 有載入背景修補才有
    };
    callbacks.submit = [this](core::PipelineJob job) {
        if (worker_ != nullptr) {
            worker_->submit(std::move(job));
        }
    };
    callbacks.cancel = [this] {
        if (worker_ != nullptr) {
            worker_->cancel(WebService::kLensId);
        }
    };
    callbacks.log = [](const std::string& message) { platform::logInfo(message); };
    web_ = std::make_unique<WebService>(platform::webPipeName(), std::move(callbacks));
    if (web_->start()) {
        platform::logInfo("網頁漫畫：等待瀏覽器擴充功能連線");
    } else {
        platform::logWarn("網頁漫畫：開不了管道（可能有另一個執行個體），瀏覽器擴充功能連不上");
        web_.reset();
    }
}

void AppController::openSettings() {
    if (settingsWindow_ != nullptr) {
        settingsWindow_->show();
        settingsWindow_->raise();
        settingsWindow_->activateWindow();
        return;
    }
    settingsWindow_ = std::make_unique<ui::SettingsWindow>(
        settings_, [](const std::string& key) { return platform::encryptSecret(key); });
    QObject::connect(settingsWindow_.get(), &ui::SettingsWindow::saved, settingsWindow_.get(),
                     [this](const core::Settings& settings) { applySettings(settings); });
    settingsWindow_->show();
}

void AppController::applySettings(const core::Settings& settings) {
    // 只取設定視窗管的欄位。結果視窗的位置和字級是它自己隨時存回 settings_ 的，
    // 設定視窗拿到的是打開當下的副本，整包蓋回去會把之後的移動和縮放洗掉。
    settings_.engines = settings.engines;
    settings_.verboseDiagnostics = settings.verboseDiagnostics;
    settings_.checkUpdates = settings.checkUpdates;
    platform::setVerboseDiagnostics(settings_.verboseDiagnostics);
    if (settings.mangaMode != settings_.mangaMode) {
        setMangaMode(settings.mangaMode);  // 載入失敗時它會把 settings_.mangaMode 留在 false
    }
    if (!(settings.hotkeys == settings_.hotkeys)) {
        unregisterHotkeys();
        settings_.hotkeys = settings.hotkeys;
        const std::vector<std::string> failed = registerHotkeys();
        if (!failed.empty()) {
            // 剛按下存檔，馬上告訴使用者哪一組不能用（被其他程式佔用）
            std::string list;
            for (const std::string& one : failed) {
                list += (list.empty() ? "" : "、") + one;
                platform::logWarn("快捷鍵 " + one + " 已經被其他程式佔用");
            }
            QMessageBox::warning(nullptr, QStringLiteral("快捷鍵"),
                                 QString::fromStdString("這些快捷鍵已經被其他程式佔用，沒有生效：" +
                                                        list + "。請換一組。"));
        }
    }
    if (settings.gameMode != settings_.gameMode) {
        setGameMode(settings.gameMode);
    }
    if (settings.ocrLanguage != settings_.ocrLanguage) {
        setOcrLanguage(settings.ocrLanguage);
    }
    if (settings.overlayFont != settings_.overlayFont) {
        settings_.overlayFont = settings.overlayFont;
        applyOverlayFont();
    }
    // 這兩項改的是目前情境的值（切走情境時會存回去）；處理管線在下面重建時套用
    settings_.dropEdgeBlocks = settings.dropEdgeBlocks;
    settings_.settleMs = settings.settleMs;
    for (const std::unique_ptr<Lens>& lens : lenses_) {
        lens->trigger->setSettleTime(std::chrono::milliseconds(settings_.settleMs));
    }
    if (!settingsPath_.empty()) {
        platform::saveSettings(settingsPath_, settings_);
    }
    // 換引擎之後馬上生效，不必重新啟動
    rebuildTranslation();
}

void AppController::setDebugOverlayEnabled(bool enabled) {
    debugOverlayEnabled_ = enabled;
    for (const std::unique_ptr<Lens>& lens : lenses_) {
        if (enabled) {
            attachOverlays(*lens);
        } else {
            lens->debugOverlay.reset();
        }
    }
    platform::logInfo(enabled ? "打開除錯覆蓋框" : "關閉除錯覆蓋框");
}

void AppController::attachOverlays(Lens& lens) {
    const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    if (debugOverlayEnabled_ && lens.debugOverlay == nullptr) {
        try {
            lens.debugOverlay = std::make_unique<platform::DebugOverlayWindow>(instance);
            // 剛打開就畫一次，不必等下一個結果
            lens.debugOverlayGeneration = 0;
            lens.debugOverlayRect = {};
            refreshDebugOverlay(lens);
        } catch (const std::exception& error) {
            platform::logWarn(std::string("打不開除錯覆蓋框：") + error.what());
        }
    }
    if (overlayRenderer_ != nullptr && lens.overlay == nullptr) {
        try {
            lens.overlay = std::make_unique<platform::TranslationOverlayWindow>(instance);
            lens.overlayGeneration = 0;
            refreshOverlay(lens);
        } catch (const std::exception& error) {
            platform::logWarn(std::string("無法在原位顯示譯文：") + error.what());
        }
    }
}

void AppController::refreshDebugOverlay(Lens& lens) {
    if (lens.debugOverlay == nullptr) {
        return;
    }
    if (!lens.window->isVisible()) {
        lens.debugOverlay->hide();
        return;
    }
    const core::RectI rect = lens.window->contentScreenRect();
    const core::LensState state = lens.trigger->state();
    const std::uint64_t generation = lens.lastResult.has_value() ? lens.lastResult->generation : 0;
    // 每 100 毫秒都會走到這裡，沒變就不重畫
    if (state == lens.debugOverlayState && generation == lens.debugOverlayGeneration &&
        rect == lens.debugOverlayRect && lens.debugOverlay->isVisible()) {
        return;
    }
    lens.debugOverlayState = state;
    lens.debugOverlayGeneration = generation;
    lens.debugOverlayRect = rect;

    const core::DebugOverlay overlay =
        lens.lastResult.has_value()
            ? core::buildDebugOverlay(*lens.lastResult, rect, stateName(state))
            : core::buildDebugOverlay(stateName(state));
    lens.debugOverlay->update(rect, overlay);
}

void AppController::setOverlayEnabled(bool enabled) {
    settings_.overlay = enabled;
    if (!enabled) {
        for (const std::unique_ptr<Lens>& lens : lenses_) {
            lens->overlay.reset();
        }
        overlayRenderer_.reset();
        inpainter_.reset();  // 釋放 LaMa 占的記憶體
        return;
    }
    if (overlayRenderer_ != nullptr) {
        return;
    }
    try {
        overlayRenderer_ = std::make_unique<platform::OverlayRenderer>();
    } catch (const std::exception& error) {
        platform::logWarn(std::string("無法在原位顯示譯文：") + error.what());
        settings_.overlay = false;
        return;
    }
    applyOverlayFont();
    setUpInpainter();
    for (const std::unique_ptr<Lens>& lens : lenses_) {
        attachOverlays(*lens);
    }
    platform::logInfo("在原位顯示譯文");
}

void AppController::setUpInpainter() {
    // 背景修補（M4-01）只用顯示卡：CPU 上每塊要 1.2～1.5 秒，照設計改用純色填補
    if (ocr_ == nullptr || ocr_->device() != ocr::Device::DirectML) {
        platform::logInfo("背景修補：OCR 沒有用顯示卡，背景不是純色的段落用純色或不蓋");
        return;
    }
    const std::filesystem::path model =
        ocr::LamaInpainter::modelPath(platform::findModelsDirectory());
    if (!std::filesystem::exists(model)) {
        platform::logInfo("背景修補：沒有 LaMa 模型（" + platform::pathToUtf8(model) +
                          "），背景不是純色的段落用純色或不蓋");
        return;
    }
    // 第一次真的需要修補時才載入模型
    inpainter_ = std::make_shared<ocr::LamaInpainter>(model);
    platform::logInfo("背景修補：可以用（第一次需要時載入 LaMa）");
}

void AppController::applyOverlayFont() {
    if (overlayRenderer_ == nullptr) {
        return;
    }
    const std::string_view file = core::overlayFontFile(settings_.overlayFont);
    const std::filesystem::path models = platform::findModelsDirectory();
    const std::filesystem::path path =
        file.empty() || models.empty()
            ? std::filesystem::path()
            : models / L"fonts" / std::filesystem::path(std::string(file));
    if (!file.empty() && (path.empty() || !overlayRenderer_->setFont(path))) {
        // 字型還沒下載（tools/fetch_models --group fonts）：照樣顯示，用微軟正黑體
        platform::logWarn("找不到譯文字型 " + std::string(file) + "，改用微軟正黑體");
        overlayRenderer_->setFont({});
    } else if (file.empty()) {
        overlayRenderer_->setFont({});
    }
    for (const std::unique_ptr<Lens>& lens : lenses_) {
        lens->overlayGeneration = 0;  // 換了字型要重畫
        refreshOverlay(*lens);
    }
}

void AppController::refreshOverlay(Lens& lens) {
    if (lens.overlay == nullptr || overlayRenderer_ == nullptr) {
        return;
    }
    // 結果要是透鏡「現在」底下的畫面：透鏡移動過、畫面變了、正在重新處理，譯文都會錯位
    const std::optional<core::PipelineResult>& result = lens.lastResult;
    const bool current = lens.window->isVisible() && !paused_ && result.has_value() &&
                         lens.trigger->state() == core::LensState::Showing &&
                         result->region == lens.window->contentScreenRect() &&
                         !result->overlay.empty();
    if (!current) {
        lens.overlay->hide();
        lens.overlayGeneration = 0;
        return;
    }
    // 每 100 毫秒都會走到這裡，同一個結果不重畫
    if (result->generation == lens.overlayGeneration && lens.overlay->isVisible()) {
        return;
    }
    const core::RectI& region = result->region;
    try {
        const core::ImageBgra image =
            overlayRenderer_->render(core::SizeI{region.width(), region.height()}, result->overlay);
        lens.overlay->show(core::PointI{region.left, region.top}, image);
        lens.overlayGeneration = result->generation;
    } catch (const std::exception& error) {
        platform::logWarn(std::string("畫不出譯文覆蓋層：") + error.what());
        lens.overlay->hide();
    }
}

std::filesystem::path AppController::writeDebugDump() {
    // 擷取當下的畫面。失敗（例如透鏡藏起來了）不算致命，報告照樣寫。
    // 除錯傾印看的是最後拖動過的透鏡
    Lens& lens = activeLens();
    std::optional<core::ImageBgra> capture;
    if (capture_ != nullptr) {
        try {
            capture = capture_->readRegion(lens.window->contentScreenRect());
        } catch (const std::exception& error) {
            platform::logWarn(std::string("除錯傾印擷取不到畫面：") + error.what());
        }
    }

    platform::DebugDumpContents contents;
    contents.report.appVersion = TMW_VERSION;
    contents.report.time = localTimeText();
    contents.report.ocrDevice =
        ocr_ == nullptr ? "（沒有 OCR）" : std::string(ocr::deviceName(ocr_->device()));
    contents.report.engineStatus =
        translation_ == nullptr ? "（沒有翻譯引擎）" : translation_->engineStatus();
    contents.report.settings = settings_;
    contents.report.lastResult = lens.lastResult;
    contents.report.perfReport = perf_.report();
    if (lens.lastResult.has_value()) {
        contents.report.lastLines = lens.lastResult->lines;
    }
    contents.capture = capture.has_value() ? &*capture : nullptr;
    contents.logsDirectory = dataDirectory_ / L"logs";

    const std::filesystem::path folder = platform::writeDebugDump(
        platform::dumpsDirectory(dataDirectory_), contents, std::chrono::system_clock::now());
    if (!folder.empty()) {
        // 使用者按下快捷鍵就是要拿這個資料夾，直接開給他看
        ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    return folder;
}

void AppController::onPipelineResult(const core::PipelineResult& result) {
    if (result.lens == WebService::kLensId) {
        if (web_ != nullptr) {
            web_->onResult(result);
        }
        return;
    }
    const platform::LogContext context{.lens = result.lens, .sequence = result.generation};
    Lens* const lens = findLens(result.lens);
    if (lens == nullptr) {
        platform::log(platform::LogLevel::Info, context, "透鏡已經關掉，丟棄");
        return;
    }
    if (!lens->trigger->onProcessingFinished(result.generation)) {
        platform::log(platform::LogLevel::Info, context, "結果已經過時，丟棄");
        return;
    }
    lens->lastResult = result;  // 除錯傾印要的是「最後真的處理過什麼」
    if (settings_.gameMode) {
        // 下一次只看這些文字在的地方有沒有變（沒讀到文字時回到看整個範圍）
        std::vector<core::RectI> textRegions;
        textRegions.reserve(result.lines.size());
        for (const core::OcrLine& line : result.lines) {
            textRegions.push_back(line.rect);
        }
        lens->trigger->setFocusRegions(textRegions);
    }
    perf_.add(result.timings);
    refreshDebugOverlay(*lens);
    refreshOverlay(*lens);
    if (!result.error.empty()) {
        platform::log(platform::LogLevel::Warn, context, "翻譯失敗：" + result.error);
    }
    platform::log(platform::LogLevel::Info, context,
                  "辨識到 " + std::to_string(result.groups.size()) + " 組，共 " +
                      std::to_string(static_cast<int>(result.timings.totalMs())) + " ms" +
                      (result.timings.ocrPrepared ? "（OCR 在等畫面穩定時就做好了）" : ""));
    if (result.groups.empty() || result.unchanged) {
        return;
    }
    const std::optional<core::HistoryCard> card =
        history_.add(result, std::chrono::system_clock::now());
    if (!card) {
        return;
    }
    for (const core::HistoryGroup& group : card->groups) {
        platform::log(
            platform::LogLevel::Info, context,
            platform::sensitive(group.source) + " → " + platform::sensitive(group.translation));
    }
    resultWindow_->addCard(*card);
    // 視窗還沒開著才把它叫出來。透鏡放在一直變的內容上（遊戲、網頁漫畫）時，
    // 每隔幾秒把結果視窗拉到遊戲畫面前面是不能用的；要一直在最上層的話有「置頂」可以開。
    // 譯文已經蓋在原文上時也不叫：使用者看的是透鏡，結果視窗是要回頭看才開的。
    if (!resultWindow_->isVisible() && overlayRenderer_ == nullptr) {
        showResultWindow();
    }
}

void AppController::showResultWindow() {
    if (resultWindow_ == nullptr) {
        return;
    }
    resultWindow_->show();
    resultWindow_->raise();
}

void AppController::setPaused(bool paused) {
    paused_ = paused;
    for (const std::unique_ptr<Lens>& lens : lenses_) {
        lens->trigger->setEnabled(!paused_ && lens->window->isVisible());
        if (paused_ && worker_ != nullptr) {
            worker_->cancel(lens->id);
        }
        updateAccent(*lens);
    }
}

void AppController::saveSettings() {
    if (resultWindow_ == nullptr || settingsPath_.empty()) {
        return;
    }
    settings_.resultWindow.geometry = resultWindow_->savedGeometry();
    settings_.resultWindow.fontPoints = resultWindow_->fontPointSize();
    settings_.resultWindow.alwaysOnTop = resultWindow_->alwaysOnTop();
    platform::saveSettings(settingsPath_, settings_);
}

bool AppController::saveLensCapture(Lens& lens) {
    const std::optional<core::ImageBgra> image =
        capture_->readRegion(lens.window->contentScreenRect());
    if (!image) {
        return false;
    }
    try {
        platform::savePng(*image,
                          platform::capturesDirectory(dataDirectory_) / timestampedCaptureName());
        return true;
    } catch (const std::exception& error) {
        OutputDebugStringA(error.what());
        return false;
    }
}

void AppController::maybeCheckForUpdates() {
    if (!settings_.checkUpdates || updateCheck_.joinable() ||
        std::chrono::steady_clock::now() - startedAt_ < kFirstUpdateCheckDelay) {
        return;
    }
    const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    if (now - settings_.lastUpdateCheck < kUpdateCheckIntervalSeconds) {
        return;
    }
    settings_.lastUpdateCheck = now;  // 連不上也算檢查過，明天再試
    saveSettings();
    updateCheck_ = std::jthread([this](std::stop_token stop) {
        // 背景執行緒：連 GitHub 最多 10 秒，結果排回 UI 執行緒
        net::CprHttpClient http;
        std::optional<net::ReleaseInfo> release = net::fetchLatestRelease(http, stop);
        if (stop.stop_requested()) {
            return;
        }
        QMetaObject::invokeMethod(
            QApplication::instance(),
            [this, release = std::move(release)] { onUpdateChecked(release); },
            Qt::QueuedConnection);
    });
}

void AppController::onUpdateChecked(std::optional<net::ReleaseInfo> release) {
    updateCheck_.join();  // 已經做完了，讓下一次可以再開
    if (!release) {
        platform::logInfo("檢查新版本：沒有找到（還沒有發行，或連不上 GitHub）");
        return;
    }
    if (!net::isNewerVersion(release->version, TMW_VERSION)) {
        platform::logInfo("檢查新版本：已經是最新的（" + std::string(TMW_VERSION) + "）");
        return;
    }
    platform::logInfo("檢查新版本：有新版本 " + release->version);
    availableUpdate_ = std::move(release);
    if (tray_ != nullptr) {
        tray_->notify(L"Translation Magic Window 有新版本",
                      L"新版本 " + platform::utf8ToWide(availableUpdate_->version) +
                          L" 可以下載了。在系統匣的選單裡點「下載新版本」。");
    }
}

void AppController::updateAccent(Lens& lens) {
    if (lens.window != nullptr && !lens.flashing) {
        lens.window->setAccent(accentFor(lens.trigger->state()));
    }
}

void AppController::flashLens(Lens& lens, core::Rgba accent) {
    lens.flashing = true;
    lens.window->setAccent(accent);
    SetTimer(hwnd_, kTimerRestoreAccent, kFlashMilliseconds, nullptr);
}

}  // namespace tmw::app
