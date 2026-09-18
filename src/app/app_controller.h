#pragma once

#include <windows.h>

#include <memory>

#include "platform/lens_window.h"
#include "platform/screen_capture.h"
#include "platform/tray_icon.h"

namespace tmw::app {

// 程式的主控：一個看不見的視窗負責接收系統匣事件和其他執行個體的通知，
// 並管理透鏡與系統匣圖示。
class AppController {
public:
    explicit AppController(HINSTANCE instance);
    ~AppController();

    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    // 執行訊息迴圈，直到使用者選擇「結束」。
    int run();

    // 由第二個執行個體呼叫：通知已在執行的執行個體把透鏡顯示出來。
    static void notifyRunningInstance();

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void showTrayMenu(POINT anchor);

    // 把透鏡範圍擷取下來存成 PNG（M0-06 的驗證工具），並用邊框顏色回報成功或失敗
    void captureLensToFile();
    void flashLens(core::Rgba accent);

    HWND hwnd_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    UINT showLensMessage_ = 0;
    bool captureHotkeyRegistered_ = false;
    std::unique_ptr<platform::ScreenCapture> capture_;
    std::unique_ptr<platform::TrayIcon> tray_;
    std::unique_ptr<platform::LensWindow> lens_;
};

}  // namespace tmw::app
