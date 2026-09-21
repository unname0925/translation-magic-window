// 真正會連網路的 HTTP 用戶端（cpr／libcurl）。
//
// 同一個用戶端重複使用同一條連線（cpr::Session 內部就是同一個 curl handle）：
// M0-12 實測連線成本占了本機 LLM 大部分的時間。所以每個引擎各自持有一個用戶端。
// cpr::Session 不是執行緒安全的，這裡用一個鎖保護；要並行就開多個用戶端。
#pragma once

#include <memory>
#include <mutex>
#include <stop_token>

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
    std::mutex mutex_;
    std::unique_ptr<cpr::Session> session_;
};

// 連本機服務要用 127.0.0.1，不要用 localhost：在 Windows 上 localhost 會先試 IPv6 再退回
// IPv4，每個請求多花約 2 秒（M0-12 實測 2.2 秒對 0.15 秒）。使用者在設定裡填 localhost 時
// 自動換掉。
std::string preferIpv4Loopback(std::string_view url);

}  // namespace tmw::net
