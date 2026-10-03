// 真正會連網路的 HTTP 用戶端（cpr／libcurl）。
//
// 同一個用戶端重複使用連線（cpr::Session 內部就是一個 curl handle）：
// M0-12 實測連線成本占了本機 LLM 大部分的時間。所以每個引擎各自持有一個用戶端。
// cpr::Session 不是執行緒安全的：同時有好幾個請求時，每個請求借一個自己的 Session
// （沒有閒置的就開一個新的），用完放回去給下一個請求沿用連線。網頁漫畫同時翻譯好幾頁時，
// 請求才不會在這裡排隊（本機 Ollama 開平行處理時 4 頁同時送快 2.3 倍）。
#pragma once

#include <memory>
#include <mutex>
#include <stop_token>
#include <vector>

#include "net/http_client.h"

namespace cpr {
class Session;
}

namespace tmw::net {

class CprHttpClient final : public IHttpClient {
public:
    CprHttpClient();
    ~CprHttpClient() override;

    CprHttpClient(const CprHttpClient&) = delete;
    CprHttpClient& operator=(const CprHttpClient&) = delete;

    HttpResponse send(const HttpRequest& request, std::stop_token cancel) override;

private:
    std::unique_ptr<cpr::Session> borrow();
    void giveBack(std::unique_ptr<cpr::Session> session);

    std::mutex mutex_;  // 只保護 idle_
    std::vector<std::unique_ptr<cpr::Session>> idle_;
};

// 連本機服務要用 127.0.0.1，不要用 localhost：在 Windows 上 localhost 會先試 IPv6 再退回
// IPv4，每個請求多花約 2 秒（M0-12 實測 2.2 秒對 0.15 秒）。使用者在設定裡填 localhost 時
// 自動換掉。
std::string preferIpv4Loopback(std::string_view url);

}  // namespace tmw::net
