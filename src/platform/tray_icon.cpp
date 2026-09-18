#include "platform/tray_icon.h"

#include <cwchar>

namespace tmw::platform {

TrayIcon::TrayIcon(HWND owner, UINT callbackMessage, HICON icon, const std::wstring& tooltip) {
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data_.uCallbackMessage = callbackMessage;
    data_.hIcon = icon;
    wcsncpy_s(data_.szTip, tooltip.c_str(), _TRUNCATE);
    add();
}

TrayIcon::~TrayIcon() {
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &data_);
    }
}

bool TrayIcon::add() {
    // Explorer 重新啟動後，舊的圖示已經不在了，先刪再加可以避免重複
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &data_);
        added_ = false;
    }
    if (!Shell_NotifyIconW(NIM_ADD, &data_)) {
        return false;
    }
    added_ = true;
    NOTIFYICONDATAW version = data_;
    version.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &version);
    return true;
}

}  // namespace tmw::platform
