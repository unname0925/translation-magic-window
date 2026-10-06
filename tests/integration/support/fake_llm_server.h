// 假的 OpenAI 相容翻譯伺服器：只聽本機，收到 /chat/completions 就把每一段翻成「譯:原文」，
// 用串流（text/event-stream）回傳。整合測試用它代替真的翻譯引擎，不連網、不會被限流。
#pragma once

#include <winsock2.h>

#include <atomic>
#include <string>
#include <thread>

namespace tmw::test {

class FakeLlmServer {
public:
    // 在 127.0.0.1 的隨機埠開始接受連線
    FakeLlmServer();
    ~FakeLlmServer();

    FakeLlmServer(const FakeLlmServer&) = delete;
    FakeLlmServer& operator=(const FakeLlmServer&) = delete;

    // 給設定檔用的端點，例如 http://127.0.0.1:52123/v1
    std::string endpoint() const;
    // 收到幾次翻譯請求
    int requests() const { return requests_.load(); }

private:
    void serve();
    void answer(SOCKET client);

    SOCKET listener_ = INVALID_SOCKET;
    int port_ = 0;
    std::atomic<bool> stopping_{false};
    std::atomic<int> requests_{0};
    std::thread thread_;
};

}  // namespace tmw::test
