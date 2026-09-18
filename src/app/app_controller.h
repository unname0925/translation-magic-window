#pragma once

#include <windows.h>

#include <filesystem>
#include <memory>

#include "core/auto_trigger.h"
#include "core/clock.h"
#include "platform/capture_frame_source.h"
#include "platform/lens_window.h"
#include "platform/screen_capture.h"
#include "platform/tray_icon.h"

namespace tmw::app {

// 程式的主控：一個看不見的視窗負責接收系統匣事件、計時器和其他執行個體的通知，
// 並把透鏡、螢幕擷取和自動觸發串起來。
class AppController {
public:
    // dataDirectory：程式寫出的檔案（目前是擷取的 PNG）要放在哪裡
    AppController(HINSTANCE instance, std::filesystem::path dataDirectory);
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
    void setLensVisible(bool visible);

    // 畫面穩定後的「處理」。M0-07 是把透鏡範圍存成 PNG；M1 會換成 OCR 和翻譯。
    void process(const core::ProcessRequest& request);

    // 把透鏡範圍擷取下來存成 PNG（開發用的驗證工具）
    bool saveLensCapture();

    // 邊框顏色：平常依照觸發狀態，手動擷取時短暫閃一下成功或失敗的顏色
    void updateAccent();
    void flashLens(core::Rgba accent);

    std::filesystem::path dataDirectory_;
    HWND hwnd_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    UINT showLensMessage_ = 0;
    bool captureHotkeyRegistered_ = false;
    bool autoSave_ = true;
    bool flashing_ = false;

    // 宣告順序就是建構順序；解構時反過來，透鏡最先消失，不會再觸發回呼
    core::SteadyClock clock_;
    std::unique_ptr<platform::ScreenCapture> capture_;
    std::unique_ptr<platform::CaptureFrameSource> frameSource_;
    std::unique_ptr<core::AutoTrigger> trigger_;
    std::unique_ptr<platform::TrayIcon> tray_;
    std::unique_ptr<platform::LensWindow> lens_;
};

}  // namespace tmw::app
