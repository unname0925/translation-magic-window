// 網頁漫畫整頁翻譯：主程式和 tmw_web_host.exe 之間的具名管道（core/web_protocol.h 的封包）。
//
// 安全：
// - 管道的存取控制只列目前的使用者：同一台電腦的其他帳號打不開。
//   網路上的連線也一律拒絕。
// - 名稱包含使用者的 SID，不同帳號用不同的管道。
// - 用戶端連上之後檢查「開管道的程序」也是目前的使用者：別的帳號就算先搶用了同一個名稱，
//   也拿不到瀏覽器送來的圖片。
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace tmw::platform {

// 目前使用者專用的管道名稱：\\.\pipe\TranslationMagicWindow.web.<SID>
std::wstring webPipeName();

// 從管道讀一則封包（4 位元組長度＋內容）。stop 被觸發、對方關閉或出錯時回傳 nullopt。
// maxBytes：超過就當成出錯（不是我們的訊息）
std::optional<std::string> readWebFrame(HANDLE pipe, HANDLE stop, std::size_t maxBytes);
// 寫一則封包（呼叫端負責同一時間只有一個執行緒在寫）
bool writeWebFrame(HANDLE pipe, std::string_view message);

class WebPipeServer {
public:
    // 在背景執行緒上呼叫。connection 是這條連線的編號（回覆時用）
    using OnMessage = std::function<void(int connection, std::string message)>;
    using OnDisconnect = std::function<void(int connection)>;

    WebPipeServer(std::wstring name, OnMessage onMessage, OnDisconnect onDisconnect);
    ~WebPipeServer();

    WebPipeServer(const WebPipeServer&) = delete;
    WebPipeServer& operator=(const WebPipeServer&) = delete;

    // 建立管道開始接受連線。同名的管道已經存在（另一個執行個體）或建立失敗時回傳 false，
    // 原因（GetLastError）在 lastError()
    bool start();
    DWORD lastError() const { return lastError_; }
    void stop();

    // 送一則訊息給某條連線（任何執行緒都可以呼叫）。連線已經斷了時回傳 false
    bool send(int connection, std::string_view message);

private:
    struct Connection;
    void acceptLoop(HANDLE first);
    void serve(int id, std::shared_ptr<Connection> connection);
    HANDLE createInstance(bool first);

    std::wstring name_;
    OnMessage onMessage_;
    OnDisconnect onDisconnect_;
    HANDLE stop_ = nullptr;  // 手動重設的事件：觸發後所有等待都會醒來
    std::thread acceptThread_;
    std::mutex mutex_;
    std::map<int, std::shared_ptr<Connection>> connections_;
    int nextId_ = 1;
    std::atomic<bool> running_{false};
    DWORD lastError_ = 0;
};

// 用戶端：連上主程式的管道。主程式還沒開管道時等到 timeoutMs 為止。
// 伺服器不是目前的使用者開的時候關掉連線、回傳 INVALID_HANDLE_VALUE
HANDLE connectWebPipe(const std::wstring& name, DWORD timeoutMs);

}  // namespace tmw::platform
