#include "net/openai_translator.h"

#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "core/translation_alignment.h"
#include "net/cpr_http_client.h"
#include "net/llm_prompt.h"
#include "net/sse.h"

namespace tmw::net {
namespace {

using core::TranslateError;
using core::TranslatorError;

}  // namespace

std::string buildChatRequest(const OpenAiTranslator::Options& options,
                             const core::TranslateRequest& request,
                             std::span<const std::string> segments, bool asJsonArray) {
    nlohmann::json body;
    body["model"] = options.model;
    body["temperature"] = options.temperature;
    if (options.maxTokens > 0) {
        body["max_tokens"] = options.maxTokens;
    }
    body["stream"] = options.stream;
    body["messages"] = nlohmann::json::array(
        {{{"role", "system"}, {"content", llmSystemPrompt(request, asJsonArray)}},
         {{"role", "user"}, {"content", llmUserMessage(request, segments, asJsonArray)}}});
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
    std::string received;              // 串流收到的全部原文（失敗時用來找錯誤說明）
    std::size_t reportedSegments = 0;  // 已經通知過的段數
    if (options_.stream) {
        http.headers.emplace_back("Accept", "text/event-stream");
        http.onChunk = [&](std::string_view chunk) {
            pending.append(chunk);
            received.append(chunk);
            for (const std::string& event : takeSseData(pending)) {
                content += streamedContent(event);
            }
            if (reportSegments && onSegment_) {
                // 收完一段就先顯示一段（design.md 4.7）
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
        // 串流時錯誤內容也是從回呼收到的，response.body 是空的；要從收到的原文找 API 的錯誤說明
        HttpResponse failed = response;
        if (failed.body.empty()) {
            failed.body = received;
        }
        throw TranslatorError(classifyHttpFailure(failed), describeHttpFailure(failed));
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
