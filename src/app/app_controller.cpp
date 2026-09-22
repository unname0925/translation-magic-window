#include "app/app_controller.h"

#include <windowsx.h>

#include <QApplication>
#include <chrono>
#include <exception>
#include <optional>
#include <string>
#include <utility>

#include "app/app_identity.h"
#include "app/translation_setup.h"
#include "core/opencc_converter.h"
#include "platform/app_paths.h"
#include "platform/logging.h"
#include "platform/png_file.h"
#include "platform/settings_file.h"
#include "platform/win_error.h"

namespace tmw::app {
namespace {

constexpr wchar_t kShowLensMessageName[] = L"TranslationMagicWindow.ShowLens";
constexpr UINT kTrayCallbackMessage = WM_APP + 1;

constexpr int kHotkeyCapture = 1;
constexpr int kHotkeyTranslate = 2;
constexpr UINT_PTR kTimerRestoreAccent = 1;
constexpr UINT_PTR kTimerTick = 2;
constexpr UINT kFlashMilliseconds = 400;
constexpr UINT kTickMilliseconds = 100;

// 擷取範圍的預設大小（96 DPI 基準）
constexpr core::SizeI kDefaultContentSize{480, 270};

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

}  // namespace

AppController::AppController(HINSTANCE instance, std::filesystem::path dataDirectory,
                             std::filesystem::path settingsPath, core::Settings settings)
    : dataDirectory_(std::move(dataDirectory)),
      settingsPath_(std::move(settingsPath)),
      settings_(std::move(settings)) {
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

        core::AutoTrigger::Callbacks triggerCallbacks;
        triggerCallbacks.onStateChanged = [this](core::LensState) { updateAccent(); };
        triggerCallbacks.onProcess = [this](const core::ProcessRequest& request) {
            process(request);
        };
        trigger_ = std::make_unique<core::AutoTrigger>(
            clock_, *frameSource_, core::AutoTriggerConfig{}, std::move(triggerCallbacks));

        platform::LensWindow::Callbacks lensCallbacks;
        lensCallbacks.onMoveSizeStart = [this] { trigger_->onMoveSizeStart(); };
        lensCallbacks.onMoveSizeEnd = [this] {
            trigger_->onMoveSizeEnd(lens_->contentScreenRect());
        };
        lens_ = std::make_unique<platform::LensWindow>(instance, kDefaultContentSize,
                                                       std::move(lensCallbacks));
        trigger_->onMoveSizeEnd(lens_->contentScreenRect());
        updateAccent();

        tray_ = std::make_unique<platform::TrayIcon>(hwnd_, kTrayCallbackMessage,
                                                     LoadIconW(nullptr, IDI_APPLICATION),
                                                     L"Translation Magic Window");

        // 快捷鍵被其他程式佔用時不算錯誤，系統匣選單一樣可以操作
        captureHotkeyRegistered_ =
            RegisterHotKey(hwnd_, kHotkeyCapture, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,
                           'S') != FALSE;
        translateHotkeyRegistered_ =
            RegisterHotKey(hwnd_, kHotkeyTranslate,
                           MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'T') != FALSE;

        setUpPipeline();

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
                setLensVisible(!lens_->isVisible());
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
        setLensVisible(true);
        return 0;
    }

    switch (message) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kCommandToggleLens:
                    setLensVisible(!lens_->isVisible());
                    break;
                case kCommandCapture:
                    if (lens_->isVisible()) {
                        flashLens(saveLensCapture() ? kSuccessAccent : kErrorAccent);
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
                case kCommandTogglePause:
                    setPaused(!paused_);
                    break;
                case kCommandTranslateNow:
                    if (lens_->isVisible() && !paused_) {
                        trigger_->manualTrigger();
                    }
                    break;
                case kCommandExit:
                    DestroyWindow(hwnd_);
                    break;
                default:
                    break;
            }
            return 0;
        case WM_HOTKEY:
            if (wParam == kHotkeyCapture && lens_->isVisible()) {
                flashLens(saveLensCapture() ? kSuccessAccent : kErrorAccent);
            } else if (wParam == kHotkeyTranslate && lens_->isVisible() && !paused_) {
                trigger_->manualTrigger();
            }
            return 0;
        case WM_TIMER:
            if (wParam == kTimerTick) {
                trigger_->tick();
            } else if (wParam == kTimerRestoreAccent) {
                KillTimer(hwnd_, kTimerRestoreAccent);
                flashing_ = false;
                updateAccent();
            }
            return 0;
        case WM_DISPLAYCHANGE:
            // 解析度或螢幕配置改變：重建擷取，並重新等待畫面穩定
            capture_->reset();
            trigger_->onMoveSizeEnd(lens_->contentScreenRect());
            return 0;
        case WM_POWERBROADCAST:
            if (wParam == PBT_APMRESUMEAUTOMATIC) {
                capture_->reset();
                trigger_->onMoveSizeEnd(lens_->contentScreenRect());
            }
            return TRUE;
        case WM_DESTROY:
            KillTimer(hwnd_, kTimerTick);
            KillTimer(hwnd_, kTimerRestoreAccent);
            if (captureHotkeyRegistered_) {
                UnregisterHotKey(hwnd_, kHotkeyCapture);
            }
            // 依相依關係的反向順序釋放：透鏡的回呼會用到 trigger_，trigger_ 會用到 frameSource_
            lens_.reset();
            tray_.reset();
            trigger_.reset();
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
    AppendMenuW(menu, MF_STRING | (lens_->isVisible() ? MF_CHECKED : MF_UNCHECKED),
                kCommandToggleLens, L"顯示透鏡");
    AppendMenuW(menu, MF_STRING, kCommandOpenResults, L"開啟結果視窗");
    AppendMenuW(menu, MF_STRING, kCommandTranslateNow,
                translateHotkeyRegistered_ ? L"立即翻譯	Ctrl+Alt+Shift+T" : L"立即翻譯");
    AppendMenuW(menu, MF_STRING | (paused_ ? MF_CHECKED : MF_UNCHECKED), kCommandTogglePause,
                L"暫停");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (autoSave_ ? MF_CHECKED : MF_UNCHECKED), kCommandToggleAutoSave,
                L"畫面穩定後自動存成 PNG（測試用）");
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

void AppController::setLensVisible(bool visible) {
    lens_->setVisible(visible);
    // 隱藏時完全不取樣、不觸發；重新顯示時從「等待穩定」開始
    trigger_->setEnabled(visible);
}

void AppController::process(const core::ProcessRequest& request) {
    if (autoSave_ && !saveLensCapture()) {
        flashLens(kErrorAccent);
    }
    const platform::LogContext context{.lens = 1, .sequence = request.generation};
    if (worker_ == nullptr || !lens_->isVisible()) {
        platform::log(platform::LogLevel::Info, context,
                      worker_ == nullptr ? "沒有可用的處理管線，跳過" : "透鏡沒顯示，跳過");
        trigger_->onProcessingFinished(request.generation);
        return;
    }
    const core::RectI region = lens_->contentScreenRect();
    std::optional<core::ImageBgra> frame = capture_->readRegion(region);
    if (!frame) {
        platform::log(platform::LogLevel::Warn, context, "擷取透鏡範圍失敗，這次不處理");
        trigger_->onProcessingFinished(request.generation);
        return;
    }
    core::PipelineJob job;
    job.generation = request.generation;
    job.lens = 1;
    job.region = region;
    job.frame = std::move(*frame);
    job.manual = request.manual;
    platform::log(platform::LogLevel::Info, context,
                  std::string(request.manual ? "手動" : "自動") + "觸發，開始處理");
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
        ocr_ = std::make_unique<ocr::OcrService>(models, ocr::TextLanguage::JapaneseOrEnglish,
                                                 ocr::Device::Auto);
    } catch (const std::exception& error) {
        platform::logError(std::string("OCR 模型載入失敗，這次執行不會翻譯：") + error.what());
        return;
    }
    platform::logInfo(std::string("OCR 裝置：") + std::string(ocr::deviceName(ocr_->device())));

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

    pipeline_ = std::make_unique<core::Pipeline>(*ocr_, *translation_);
    worker_ =
        std::make_unique<core::PipelineWorker>(*pipeline_, [this](core::PipelineResult result) {
            // 這裡是工作執行緒。回到 UI 執行緒才能碰透鏡和結果視窗（design.md 3.3）。
            QMetaObject::invokeMethod(
                QApplication::instance(),
                [this, moved = std::move(result)] { onPipelineResult(moved); },
                Qt::QueuedConnection);
        });
}

void AppController::onPipelineResult(const core::PipelineResult& result) {
    const platform::LogContext context{.lens = result.lens, .sequence = result.generation};
    if (!trigger_->onProcessingFinished(result.generation)) {
        platform::log(platform::LogLevel::Info, context, "結果已經過時，丟棄");
        return;
    }
    if (!result.error.empty()) {
        platform::log(platform::LogLevel::Warn, context, "翻譯失敗：" + result.error);
    }
    platform::log(platform::LogLevel::Info, context,
                  "辨識到 " + std::to_string(result.groups.size()) + " 組，共 " +
                      std::to_string(static_cast<int>(result.timings.totalMs())) + " ms");
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
    showResultWindow();
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
    trigger_->setEnabled(!paused_);
    if (paused_ && worker_ != nullptr) {
        worker_->cancel(1);
    }
    updateAccent();
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

bool AppController::saveLensCapture() {
    const std::optional<core::ImageBgra> image = capture_->readRegion(lens_->contentScreenRect());
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

void AppController::updateAccent() {
    if (lens_ && !flashing_) {
        lens_->setAccent(accentFor(trigger_->state()));
    }
}

void AppController::flashLens(core::Rgba accent) {
    flashing_ = true;
    lens_->setAccent(accent);
    SetTimer(hwnd_, kTimerRestoreAccent, kFlashMilliseconds, nullptr);
}

}  // namespace tmw::app
