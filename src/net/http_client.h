// HTTP 用戶端的介面（見 docs/design.md 3.2）。
//
// 翻譯引擎只認得這個介面，測試時換成「回放錄好的回應」的假用戶端，不必連網路。
#pragma once

#include <chrono>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tmw::net {

enum class HttpMethod {
    Get,
    Post,
};

struct HttpRequest {
    std::string url;
    HttpMethod method = HttpMethod::Get;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    std::chrono::milliseconds timeout{15000};
    // 回應是一行一行送回來的（SSE）時，每收到一段就呼叫一次。回傳 false 表示不要再收了。
    // 設定了這個就不會填 HttpResponse::body。
    std::function<bool(std::string_view chunk)> onChunk;
};

struct HttpResponse {
    // HTTP 狀態碼；連不上時是 0
    int status = 0;
    std::string body;
    // 連線層的錯誤說明（空字串表示連線本身沒問題）
    std::string error;

    bool connected() const { return status != 0; }
    bool ok() const { return status >= 200 && status < 300; }
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual HttpResponse send(const HttpRequest& request, std::stop_token cancel) = 0;
};

// 把文字轉成網址查詢字串可以用的形式（RFC 3986 的 unreserved 以外都轉成 %XX）
std::string percentEncode(std::string_view text);

// 組出 "<base>?a=1&b=2"，值會自動編碼
std::string buildUrl(std::string_view base,
                     const std::vector<std::pair<std::string, std::string>>& parameters);

}  // namespace tmw::net
