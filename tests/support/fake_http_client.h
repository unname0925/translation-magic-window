// 假的 HTTP 用戶端：回放事先錄好的回應，並記下送出去的請求。
//
// 錄好的回應放在 tests/data/net（CT-01、CT-02），是真的向端點要過一次的內容。
#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "net/http_client.h"

namespace tmw::test {

inline std::string readTestData(const std::string& relativePath) {
    const std::filesystem::path path = std::filesystem::path(TMW_TEST_DATA_DIR) / relativePath;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("讀不到測試資料：" + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

class FakeHttpClient final : public net::IHttpClient {
public:
    net::HttpResponse send(const net::HttpRequest& request, std::stop_token cancel) override {
        requests.push_back(request);
        if (replies.empty()) {
            throw std::runtime_error("假用戶端沒有準備回應");
        }
        net::HttpResponse reply = replies.front();
        replies.erase(replies.begin());
        if (request.onChunk && !reply.body.empty()) {
            // 真的伺服器不會一次給完，切成小塊才能測到組裝的邏輯
            constexpr std::size_t kChunk = 24;
            for (std::size_t at = 0; at < reply.body.size(); at += kChunk) {
                if (cancel.stop_requested() ||
                    !request.onChunk(std::string_view(reply.body).substr(at, kChunk))) {
                    break;
                }
            }
            reply.body.clear();
        }
        return reply;
    }

    // 依序回放
    void reply(std::string body, int status = 200) {
        replies.push_back(net::HttpResponse{status, std::move(body), ""});
    }
    void replyFromFile(const std::string& relativePath) { reply(readTestData(relativePath)); }
    void failToConnect(std::string message = "連不上") {
        replies.push_back(net::HttpResponse{0, "", std::move(message)});
    }

    std::vector<net::HttpRequest> requests;
    std::vector<net::HttpResponse> replies;
};

}  // namespace tmw::test
