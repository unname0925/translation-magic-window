#pragma once

#include <windows.h>

#include <shellapi.h>

#include <string>

namespace tmw::platform {

// 系統匣圖示。透鏡不在工作列上、也不會取得焦點，
// 所以開啟結果視窗、暫停、結束程式都要透過這個圖示（見 docs/design.md 4.1）。
//
// 事件會以 callbackMessage 傳給 owner（NOTIFYICON_VERSION_4 格式）：
//   LOWORD(lParam) 是事件（WM_CONTEXTMENU、NIN_SELECT 等），
//   wParam 是滑鼠的螢幕座標（GET_X_LPARAM / GET_Y_LPARAM）。
class TrayIcon {
public:
    TrayIcon(HWND owner, UINT callbackMessage, HICON icon, const std::wstring& tooltip);
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    // 加入系統匣。Explorer 重新啟動（收到 "TaskbarCreated" 訊息）後要再呼叫一次。
    // Explorer 還沒準備好時會失敗並回傳 false，等 TaskbarCreated 再試即可。
    bool add();

private:
    NOTIFYICONDATAW data_{};
    bool added_ = false;
};

}  // namespace tmw::platform
