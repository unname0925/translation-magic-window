#include "net/custom_http_translator.h"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <utility>

#include "core/custom_http_check.h"
#include "net/llm_prompt.h"
#include "net/ruby_notes.h"

namespace tmw::net {
namespace {

using core::TranslateError;
using core::TranslatorError;
using json = nlohmann::json;

void replaceAll(std::string& text, std::string_view from, std::string_view to) {
    for (std::size_t at = text.find(from); at != std::string::npos;
         at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
}

// JSON 字串裡的內容（不含前後的引號）
std::string jsonEscaped(const std::string& text) {
    const std::string quoted = json(text).dump();
    return quoted.substr(1, quoted.size() - 2);
}

std::string urlEncoded(const std::string& text) {
    std::string out;
    for (const unsigned char c : text) {
        if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buffer[4];
            std::snprintf(buffer, sizeof(buffer), "%%%02X", c);
            out += buffer;
        }
    }
    return out;
}

std::string trimmed(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string sourceOf(const core::TranslateRequest& request) {
    return request.srcLang.empty() ? std::string("auto") : request.srcLang;
}

}  // namespace

std::optional<std::string> extractJsonPath(std::string_view body, std::string_view path) {
    const json parsed = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        return std::nullopt;
    }
    const json* at = &parsed;
    std::size_t i = 0;
    while (i < path.size()) {
        if (path[i] == '.') {
            ++i;
            continue;
        }
        if (path[i] == '[') {
            const std::size_t close = path.find(']', i);
            if (close == std::string_view::npos || !at->is_array()) {
                return std::nullopt;
            }
            const std::string number(path.substr(i + 1, close - i - 1));
            if (number.empty() || number.find_first_not_of("0123456789") != std::string::npos) {
                return std::nullopt;
            }
            const std::size_t index = std::stoul(number);
            if (index >= at->size()) {
                return std::nullopt;
            }
            at = &(*at)[index];
            i = close + 1;
            continue;
        }
        const std::size_t end = path.find_first_of(".[", i);
        const std::string key(path.substr(i, end == std::string_view::npos ? path.npos : end - i));
        if (!at->is_object() || !at->contains(key)) {
            return std::nullopt;
        }
        at = &(*at)[key];
        i = end == std::string_view::npos ? path.size() : end;
    }
    if (!at->is_string()) {
        return std::nullopt;
    }
    return at->get<std::string>();
}

std::string customHttpProblem(const CustomHttpOptions& options) {
    return core::customHttpProblem(options.url, options.headers, options.bodyTemplate,
                                   options.responsePath);
}

CustomHttpTranslator::CustomHttpTranslator(std::shared_ptr<IHttpClient> http,
                                           CustomHttpOptions options)
    : http_(std::move(http)), options_(std::move(options)) {}

HttpRequest CustomHttpTranslator::buildRequest(const std::string& text,
                                               const core::TranslateRequest& request) const {
    const auto fill = [&](std::string target, bool forUrl) {
        replaceAll(target, "{{text}}", forUrl ? urlEncoded(text) : jsonEscaped(text));
        replaceAll(target, "{{source}}", sourceOf(request));
        replaceAll(target, "{{target}}", "zh-TW");
        replaceAll(target, "{{key}}", forUrl ? urlEncoded(options_.apiKey) : options_.apiKey);
        return target;
    };
    HttpRequest http;
    http.url = fill(options_.url, /*forUrl=*/true);
    http.timeout = options_.timeout;
    if (trimmed(options_.bodyTemplate).empty()) {
        http.method = HttpMethod::Get;
    } else {
        http.method = HttpMethod::Post;
        http.body = fill(options_.bodyTemplate, /*forUrl=*/false);
    }
    bool hasContentType = false;
    std::size_t lineStart = 0;
    while (lineStart < options_.headers.size()) {
        const std::size_t lineEnd = options_.headers.find('\n', lineStart);
        const std::string line =
            trimmed(std::string_view(options_.headers)
                        .substr(lineStart, lineEnd == std::string::npos ? std::string::npos
                                                                        : lineEnd - lineStart));
        lineStart = lineEnd == std::string::npos ? options_.headers.size() : lineEnd + 1;
        const std::size_t colon = line.find(':');
        if (line.empty() || colon == std::string::npos) {
            continue;
        }
        std::string name = trimmed(std::string_view(line).substr(0, colon));
        std::string value = fill(trimmed(std::string_view(line).substr(colon + 1)), false);
        std::string lower = name;
        for (char& c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        hasContentType = hasContentType || lower == "content-type";
        http.headers.emplace_back(std::move(name), std::move(value));
    }
    if (http.method == HttpMethod::Post && !hasContentType) {
        http.headers.emplace_back("Content-Type", "application/json");
    }
    return http;
}

std::string CustomHttpTranslator::translateOne(const std::string& text,
                                               const core::TranslateRequest& request,
                                               std::stop_token cancel) {
    const HttpResponse response = http_->send(buildRequest(text, request), cancel);
    if (cancel.stop_requested()) {
        throw TranslatorError(TranslateError::Cancelled, "翻譯被取消");
    }
    if (!response.ok()) {
        throw TranslatorError(classifyHttpFailure(response), describeHttpFailure(response));
    }
    std::optional<std::string> translated = extractJsonPath(response.body, options_.responsePath);
    if (!translated) {
        throw TranslatorError(TranslateError::BadResponse,
                              "回應裡找不到「" + options_.responsePath + "」");
    }
    return std::move(*translated);
}

std::vector<std::string> CustomHttpTranslator::translate(std::span<const std::string> segments,
                                                         const core::TranslateRequest& request,
                                                         std::stop_token cancel) {
    // 不知道對方懂不懂 `{本文|讀音}`，一律當成看不懂（net/ruby_notes）
    return translateWithRubyNotes(segments, [&](std::span<const std::string> all) {
        std::vector<std::string> out;
        out.reserve(all.size());
        for (const std::string& text : all) {
            out.push_back(translateOne(text, request, cancel));
        }
        return out;
    });
}

}  // namespace tmw::net
