#include "core/custom_http_check.h"

#include <cctype>
#include <cstddef>

namespace tmw::core {
namespace {

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

}  // namespace

std::string customHttpProblem(std::string_view url, std::string_view headers,
                              std::string_view bodyTemplate, std::string_view responsePath) {
    if (!url.starts_with("http://") && !url.starts_with("https://")) {
        return "網址要以 http:// 或 https:// 開頭";
    }
    if (trimmed(responsePath).empty()) {
        return "要填譯文在回應裡的 JSON 路徑，例如 translations[0].text";
    }
    if (url.find("{{text}}") == std::string_view::npos &&
        bodyTemplate.find("{{text}}") == std::string_view::npos) {
        return "網址或請求內容裡要有 {{text}}，不然原文送不出去";
    }
    while (!headers.empty()) {
        const std::size_t end = headers.find('\n');
        const std::string_view line = trimmed(headers.substr(0, end));
        headers.remove_prefix(end == std::string_view::npos ? headers.size() : end + 1);
        if (!line.empty() && line.find(':') == std::string_view::npos) {
            return "標頭「" + std::string(line) + "」要寫成「名稱: 值」";
        }
    }
    return {};
}

}  // namespace tmw::core
