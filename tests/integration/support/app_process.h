#pragma once

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace tmw::test {

// 在另一個程序中啟動真正的主程式（和測試程式放在同一個資料夾的
// TranslationMagicWindow.exe），讓整合測試操作的是實際會發布的程式。
// 解構時如果還在執行，先請它正常結束，逾時才強制結束。
class AppProcess {
public:
    // args 是命令列參數，不含程式名稱。無法啟動時丟出 std::runtime_error。
    explicit AppProcess(const std::vector<std::wstring>& args);
    ~AppProcess();

    AppProcess(const AppProcess&) = delete;
    AppProcess& operator=(const AppProcess&) = delete;

    static std::filesystem::path executablePath();

    // 是否已經有主程式在執行（不論是不是這個測試啟動的）。
    static bool isAnyInstanceRunning();

    DWORD processId() const { return info_.dwProcessId; }
    bool hasExited() const;

    // 等它結束，回傳結束代碼；逾時回傳 std::nullopt。
    std::optional<DWORD> waitForExit(std::chrono::milliseconds timeout);

    // 這個程序中，類別名稱為 className 的最上層視窗；找不到時回傳 nullptr。
    HWND findWindow(const wchar_t* className) const;

    // 等到視窗出現。逾時或程序提早結束時回傳 nullptr。
    HWND waitForWindow(const wchar_t* className, std::chrono::milliseconds timeout) const;

    // 這個程序中標題為 title 的可見視窗（Qt 的視窗類別名稱會隨版本改變，所以用標題找）。
    HWND findWindowByTitle(const wchar_t* title) const;
    HWND waitForWindowByTitle(const wchar_t* title, std::chrono::milliseconds timeout) const;

    // 主控視窗是否在時限內處理了一則訊息，也就是主程式有在處理訊息、不是卡住的。
    // （不用 WaitForInputIdle：主程式初始化時 COM 和系統匣就會處理訊息，它會太早回傳）
    bool isResponsive(std::chrono::milliseconds timeout) const;

    // 送出主控視窗的指令（app/app_identity.h 的 Command），就像從系統匣選單選擇一樣。
    bool postCommand(UINT command) const;

    // 請它正常結束（系統匣選單的「結束」），回傳結束代碼；逾時回傳 std::nullopt。
    std::optional<DWORD> requestExit(std::chrono::milliseconds timeout);

private:
    PROCESS_INFORMATION info_{};
};

}  // namespace tmw::test
