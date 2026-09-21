#include "net/openai_translator.h"

#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "core/translation_alignment.h"
#include "net/cpr_http_client.h"
#include "net/sse.h"

namespace tmw::net {
namespace {

using core::TranslateError;
using core::TranslatorError;

// M0-12 實測過的提示詞（盲評 4.13～4.46 分），原樣沿用
constexpr std::string_view kSystemPrompt =
    "你是翻譯引擎。把 segments 中的每個字串翻譯成台灣繁體中文，保留語氣和角色口吻。"
    "`{本文|讀音}` 是日文漫畫中作者刻意標的特殊讀音（左邊是字面，右邊是實際的意思），"
    "譯文要在對應的位置保留同樣的標記。"
    "只輸出和 segments 等長的 JSON 字串陣列，不要加任何說明。";

constexpr std::string_view kPlainPrompt =
    "你是翻譯引擎。把使用者傳來的文字翻譯成台灣繁體中文，保留語氣和角色口吻。"
    "`{本文|讀音}` 標記要保留。只輸出譯文，不要加引號或任何說明。";

TranslateError classify(const HttpResponse& response) {
    if (!response.connected()) {
        return TranslateError::Network;
    }
    if (response.status == 429) {
        return TranslateError::RateLimited;
    }
    if (response.status >= 500) {
        return TranslateError::Network;
    }
    // 其餘的 4xx（金鑰錯誤、模型名稱錯誤、被擋下來）重送同樣的內容也不會變好
    return TranslateError::Rejected;
}

// 出錯時的回應內容可能含有金鑰或原文，只留狀態碼和 API 自己的錯誤說明
std::string describeFailure(const HttpResponse& response) {
    if (!response.connected()) {
        return response.error;
    }
    std::string out = "HTTP " + std::to_string(response.status);
    const nlohmann::json parsed =
        nlohmann::json::parse(response.body, nullptr, /*allow_exceptions=*/false);
    const auto error = parsed.is_object() ? parsed.find("error") : parsed.end();
    if (error != parsed.end() && error->is_object()) {
        const auto message = error->find("message");
        if (message != error->end() && message->is_string()) {
            out += "：" + message->get<std::string>();
        }
    }
    return out;
}

}  // namespace

std::string buildChatRequest(const OpenAiTranslator::Options& options,
                             const core::TranslateRequest& request,
                             std::span<const std::string> segments, bool asJsonArray) {
    nlohmann::json user;
    if (asJsonArray) {
        user["source_lang"] = request.srcLang;
        if (!request.context.empty()) {
            nlohmann::json context = nlohmann::json::array();
            for (const auto& [source, translation] : request.context) {
                context.push_back({source, translation});
            }
            user["context"] = std::move(context);
        }
        if (!request.glossary.empty()) {
            user["glossary"] = request.glossary;
        }
        user["segments"] = std::vector<std::string>(segments.begin(), segments.end());
    }

    nlohmann::json body;
    body["model"] = options.model;
    body["temperature"] = options.temperature;
    body["stream"] = options.stream;
    body["messages"] = nlohmann::json::array(
        {{{"role", "system"}, {"content", std::string(asJsonArray ? kSystemPrompt : kPlainPrompt)}},
         {{"role", "user"}, {"content", asJsonArray ? user.dump() : std::string(segments[0])}}});
    return body.dump();
}

std::string streamedContent(std::string_view event) {
    const nlohmann::json parsed = nlohmann::json::parse(event, nullptr, /*allow_exceptions=*/false);
    if (!parsed.is_object()) {
        return {};
    }
    const auto choices = parsed.find("choices");
    if (choices == parsed.end() || !choices->is_array() || choices->empty()) {
        return {};
    }
    const nlohmann::json& first = (*choices)[0];
    const auto delta = first.find("delta");
    if (delta == first.end() || !delta->is_object()) {
        return {};
    }
    const auto content = delta->find("content");
    if (content == delta->end() || !content->is_string()) {
        return {};
    }
    return content->get<std::string>();
}

std::optional<std::string> completionContent(std::string_view body) {
    const nlohmann::json parsed = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (!parsed.is_object()) {
        return std::nullopt;
    }
    const auto choices = parsed.find("choices");
    if (choices == parsed.end() || !choices->is_array() || choices->empty()) {
        return std::nullopt;
    }
    const nlohmann::json& first = (*choices)[0];
    const auto message = first.find("message");
    if (message == first.end() || !message->is_object()) {
        return std::nullopt;
    }
    const auto content = message->find("content");
    if (content == message->end() || !content->is_string()) {
        return std::nullopt;
    }
    return content->get<std::string>();
}

OpenAiTranslator::OpenAiTranslator(std::shared_ptr<IHttpClient> http, Options options)
    : http_(std::move(http)), options_(std::move(options)) {}

std::string OpenAiTranslator::completionsUrl() const {
    std::string base = preferIpv4Loopback(options_.baseUrl);
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base + "/chat/completions";
}

std::string OpenAiTranslator::send(const std::string& body, std::stop_token cancel,
                                   bool reportSegments) {
    HttpRequest http;
    http.url = completionsUrl();
    http.method = HttpMethod::Post;
    http.body = body;
    http.timeout = options_.timeout;
    http.headers.emplace_back("Content-Type", "application/json");
    if (!options_.apiKey.empty()) {
        http.headers.emplace_back("Authorization", "Bearer " + options_.apiKey);
    }

    std::string content;
    std::string pending;               // 還沒收完的那一行
    std::size_t reportedSegments = 0;  // 已經通知過的段數
    if (options_.stream) {
        http.headers.emplace_back("Accept", "text/event-stream");
        http.onChunk = [&](std::string_view chunk) {
            pending.append(chunk);
            for (const std::string& event : takeSseData(pending)) {
                content += streamedContent(event);
            }
            if (reportSegments && onSegment_) {
                // 收完一段就先顯示一段（design.md 4.6）
                const std::vector<std::string> ready = core::parseJsonArrayPrefix(content);
                for (; reportedSegments < ready.size(); ++reportedSegments) {
                    onSegment_(reportedSegments, ready[reportedSegments]);
                }
            }
            return true;
        };
    }

    const HttpResponse response = http_->send(http, cancel);
    if (cancel.stop_requested()) {
        throw TranslatorError(TranslateError::Cancelled, "翻譯被取消");
    }
    if (!response.ok()) {
        throw TranslatorError(classify(response), describeFailure(response));
    }
    if (options_.stream) {
        return content;
    }
    std::optional<std::string> once = completionContent(response.body);
    if (!once) {
        throw TranslatorError(TranslateError::BadResponse, "看不懂 API 的回應");
    }
    return std::move(*once);
}

std::vector<std::string> OpenAiTranslator::requestArray(std::span<const std::string> segments,
                                                        const core::TranslateRequest& request,
                                                        std::stop_token cancel) {
    const std::string reply =
        send(buildChatRequest(options_, request, segments, /*asJsonArray=*/true), cancel,
             /*reportSegments=*/true);
    std::optional<std::vector<std::string>> parsed = core::parseJsonArray(reply);
    if (!parsed) {
        throw TranslatorError(TranslateError::BadResponse, "回應不是 JSON 陣列");
    }
    return std::move(*parsed);
}

std::string OpenAiTranslator::requestPlain(const std::string& segment,
                                           const core::TranslateRequest& request,
                                           std::stop_token cancel) {
    const std::span<const std::string> one(&segment, 1);
    return send(buildChatRequest(options_, request, one, /*asJsonArray=*/false), cancel,
                /*reportSegments=*/false);
}

std::vector<std::string> OpenAiTranslator::translate(std::span<const std::string> segments,
                                                     const core::TranslateRequest& request,
                                                     std::stop_token cancel) {
    const core::BatchTranslate batch =
        [&](std::span<const std::string> part) -> std::vector<std::string> {
        // 只剩一段時不要 JSON，直接要譯文：模型偶爾會在譯文裡用沒有逸出的引號，
        // 這是 design.md 4.5 的第三層退路（M0-12 實測四個引擎都靠它補完）。
        if (part.size() == 1) {
            return {requestPlain(part[0], request, cancel)};
        }
        return requestArray(part, request, cancel);
    };
    return core::translateAligned(segments, batch);
}

}  // namespace tmw::net
