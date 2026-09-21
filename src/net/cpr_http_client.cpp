#include "net/cpr_http_client.h"

#include <cpr/cpr.h>

#include <string>
#include <utility>

namespace tmw::net {

CprHttpClient::CprHttpClient() : session_(std::make_unique<cpr::Session>()) {}

CprHttpClient::~CprHttpClient() = default;

HttpResponse CprHttpClient::send(const HttpRequest& request, std::stop_token cancel) {
    const std::lock_guard lock(mutex_);
    cpr::Session& session = *session_;
    session.SetUrl(cpr::Url{request.url});

    cpr::Header header;
    for (const auto& [name, value] : request.headers) {
        header[name] = value;
    }
    session.SetHeader(header);
    session.SetTimeout(cpr::Timeout{request.timeout});
    session.SetBody(cpr::Body{request.body});

    // 使用者移動透鏡時要能馬上停下來，不必等整個回應收完
    session.SetProgressCallback(cpr::ProgressCallback{
        [&cancel](cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t,
                  intptr_t) { return !cancel.stop_requested(); }});

    bool stoppedByCallback = false;
    if (request.onChunk) {
        session.SetWriteCallback(cpr::WriteCallback{[&](std::string_view data, intptr_t) {
            if (!request.onChunk(data)) {
                stoppedByCallback = true;
                return false;
            }
            return !cancel.stop_requested();
        }});
    }

    const cpr::Response response =
        request.method == HttpMethod::Post ? session.Post() : session.Get();

    // 下一個請求不要沿用這次的回呼
    session.SetProgressCallback(cpr::ProgressCallback{});
    if (request.onChunk) {
        session.SetWriteCallback(cpr::WriteCallback{});
    }

    HttpResponse out;
    out.status = static_cast<int>(response.status_code);
    out.body = response.text;
    if (response.error) {
        // 自己叫停不算錯誤
        if (!stoppedByCallback && !cancel.stop_requested()) {
            out.error = response.error.message;
        }
    }
    if (out.status == 0 && out.error.empty()) {
        out.error = cancel.stop_requested() ? "已取消" : "連線失敗";
    }
    return out;
}

std::string preferIpv4Loopback(std::string_view url) {
    constexpr std::string_view kLocalhost = "localhost";
    const std::size_t scheme = url.find("://");
    if (scheme == std::string_view::npos) {
        return std::string(url);
    }
    const std::size_t host = scheme + 3;
    if (url.compare(host, kLocalhost.size(), kLocalhost) != 0) {
        return std::string(url);
    }
    // 只換掉主機名稱本身：localhost、localhost:11434、localhost/v1 都算，localhostfoo 不算
    const std::size_t after = host + kLocalhost.size();
    if (after < url.size() && url[after] != ':' && url[after] != '/' && url[after] != '?') {
        return std::string(url);
    }
    std::string out(url.substr(0, host));
    out += "127.0.0.1";
    out += url.substr(after);
    return out;
}

}  // namespace tmw::net
