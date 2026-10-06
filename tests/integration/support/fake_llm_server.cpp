#include "support/fake_llm_server.h"

#include <ws2tcpip.h>

#include <cctype>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

namespace tmw::test {
namespace {

// 讀到標頭結束，再照 Content-Length 讀完本文
std::string readRequestBody(SOCKET client) {
    std::string data;
    char buffer[8192];
    std::size_t headerEnd = std::string::npos;
    std::size_t length = 0;
    while (true) {
        const int received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            return {};
        }
        data.append(buffer, received);
        if (headerEnd == std::string::npos) {
            headerEnd = data.find("\r\n\r\n");
            if (headerEnd == std::string::npos) {
                continue;
            }
            std::string headers = data.substr(0, headerEnd);
            for (char& c : headers) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            const std::size_t at = headers.find("content-length:");
            if (at != std::string::npos) {
                length = std::stoul(headers.substr(at + 15));
            }
        }
        if (data.size() >= headerEnd + 4 + length) {
            return data.substr(headerEnd + 4, length);
        }
    }
}

void sendAll(SOCKET client, std::string_view text) {
    while (!text.empty()) {
        const int sent = send(client, text.data(), static_cast<int>(text.size()), 0);
        if (sent <= 0) {
            return;
        }
        text.remove_prefix(static_cast<std::size_t>(sent));
    }
}

// 最後一則使用者訊息是 {"source_lang":…,"segments":[…]}（net/llm_prompt）。每段翻成「譯:原文」
std::string translationsFor(const std::string& body) {
    const nlohmann::json request = nlohmann::json::parse(body, nullptr, false);
    nlohmann::json out = nlohmann::json::array();
    if (request.is_discarded() || !request.contains("messages")) {
        return out.dump();
    }
    const nlohmann::json& last = request["messages"].back();
    const nlohmann::json user =
        nlohmann::json::parse(last.value("content", std::string()), nullptr, false);
    if (!user.is_discarded() && user.contains("segments")) {
        for (const auto& segment : user["segments"]) {
            out.push_back("譯:" + segment.get<std::string>());
        }
    }
    return out.dump();
}

}  // namespace

FakeLlmServer::FakeLlmServer() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        throw std::runtime_error("WSAStartup 失敗");
    }
    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;  // 讓系統挑一個空的埠
    if (listener_ == INVALID_SOCKET ||
        bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener_, 8) != 0) {
        throw std::runtime_error("假的翻譯伺服器開不起來");
    }
    int size = sizeof(address);
    getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size);
    port_ = ntohs(address.sin_port);
    thread_ = std::thread([this] { serve(); });
}

FakeLlmServer::~FakeLlmServer() {
    stopping_ = true;
    closesocket(listener_);  // accept 會因此返回
    if (thread_.joinable()) {
        thread_.join();
    }
    WSACleanup();
}

std::string FakeLlmServer::endpoint() const {
    return "http://127.0.0.1:" + std::to_string(port_) + "/v1";
}

void FakeLlmServer::serve() {
    while (!stopping_) {
        const SOCKET client = accept(listener_, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            return;
        }
        answer(client);
        closesocket(client);
    }
}

void FakeLlmServer::answer(SOCKET client) {
    const std::string body = readRequestBody(client);
    ++requests_;
    // 串流：一次給完整的內容，再送 [DONE]
    const nlohmann::json chunk{
        {"choices", {{{"index", 0}, {"delta", {{"content", translationsFor(body)}}}}}}};
    const std::string events = "data: " + chunk.dump() + "\n\ndata: [DONE]\n\n";
    sendAll(client,
            "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n"
            "Content-Length: " +
                std::to_string(events.size()) + "\r\n\r\n" + events);
}

}  // namespace tmw::test
