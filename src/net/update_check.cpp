#include "net/update_check.h"

#include <array>
#include <charconv>
#include <chrono>
#include <exception>
#include <nlohmann/json.hpp>

namespace tmw::net {
namespace {

// 「v1.2.3」→ {1, 2, 3}；少的部分補 0（「1.2」= 1.2.0）
std::optional<std::array<int, 3>> parseVersion(std::string_view text) {
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) {
        text.remove_prefix(1);
    }
    std::array<int, 3> parts{0, 0, 0};
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const char* const begin = text.data();
        const char* const end = text.data() + text.size();
        const auto [next, error] = std::from_chars(begin, end, parts[i]);
        if (error != std::errc() || next == begin) {
            return std::nullopt;
        }
        text.remove_prefix(static_cast<std::size_t>(next - begin));
        if (text.empty()) {
            return parts;
        }
        if (text.front() != '.') {
            return std::nullopt;  // 「1.2.3-beta」這種預覽版不算
        }
        text.remove_prefix(1);
    }
    return text.empty() ? std::optional(parts) : std::nullopt;
}

}  // namespace

bool isNewerVersion(std::string_view candidate, std::string_view current) {
    const auto a = parseVersion(candidate);
    const auto b = parseVersion(current);
    return a && b && *a > *b;
}

std::optional<ReleaseInfo> fetchLatestRelease(IHttpClient& http, std::stop_token cancel) {
    HttpRequest request;
    request.url = std::string(kLatestReleaseUrl);
    // GitHub API 要求 User-Agent
    request.headers = {{"Accept", "application/vnd.github+json"},
                       {"User-Agent", "TranslationMagicWindow"}};
    request.timeout = std::chrono::seconds(10);
    const HttpResponse response = http.send(request, cancel);
    if (!response.ok()) {
        return std::nullopt;  // 404：還沒有發行過；其他：下次再試
    }
    try {
        const nlohmann::json document = nlohmann::json::parse(response.body);
        if (document.value("draft", false) || document.value("prerelease", false)) {
            return std::nullopt;
        }
        std::string tag = document.at("tag_name").get<std::string>();
        if (!parseVersion(tag)) {
            return std::nullopt;
        }
        if (tag.front() == 'v' || tag.front() == 'V') {
            tag.erase(0, 1);
        }
        return ReleaseInfo{tag, document.at("html_url").get<std::string>()};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace tmw::net
