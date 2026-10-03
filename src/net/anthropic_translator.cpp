#include "net/anthropic_translator.h"

#include <nlohmann/json.hpp>
#include <utility>

#include "core/translation_alignment.h"
#include "net/llm_prompt.h"
#include "net/sse.h"

namespace tmw::net {
namespace {

using core::TranslateError;
using core::TranslatorError;

// structured outputs 的根節點必須是物件，所以譯文陣列包在 translations 底下
const nlohmann::json& translationsSchema() {
    static const nlohmann::json schema = {
        {"type", "object"},
        {"properties", {{"translations", {{"type", "array"}, {"items", {{"type", "string"}}}}}}},
        {"required", {"translations"}},
        {"additionalProperties", false}};
    return schema;
}

// 結束原因不是正常結束時，譯文不能用
void checkStopReason(const std::string& stopReason) {
    if (stopReason == "refusal") {
        // 開了伺服器端的備援仍然被拒絕：重送同樣的內容不會變好
        throw TranslatorError(TranslateError::Rejected, "Claude 拒絕翻譯這段內容");
    }
    if (stopReason == "max_tokens") {
        throw TranslatorError(TranslateError::BadResponse, "回覆超過長度上限，譯文不完整");
    }
}

}  // namespace

AnthropicEvent parseAnthropicEvent(std::string_view data) {
    AnthropicEvent out;
    const nlohmann::json event = nlohmann::json::parse(data, nullptr, /*allow_exceptions=*/false);
    if (!event.is_object()) {
        return out;
    }
    const std::string type = event.value("type", "");
    if (type == "content_block_delta") {
        const auto delta = event.find("delta");
        // 思考的內容（thinking_delta、signature_delta）不是譯文
        if (delta != event.end() && delta->is_object() &&
            delta->value("type", "") == "text_delta") {
            out.text = delta->value("text", "");
        }
    } else if (type == "message_delta") {
        const auto delta = event.find("delta");
        if (delta != event.end() && delta->is_object() && delta->contains("stop_reason") &&
            (*delta)["stop_reason"].is_string()) {
            out.stopReason = (*delta)["stop_reason"].get<std::string>();
        }
    } else if (type == "error") {
        const auto error = event.find("error");
        out.error = error != event.end() && error->is_object()
                        ? error->value("message", std::string("串流中斷"))
                        : std::string("串流中斷");
    }
    return out;
}

std::optional<AnthropicReply> parseAnthropicReply(std::string_view body) {
    const nlohmann::json parsed = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (!parsed.is_object()) {
        return std::nullopt;
    }
    const auto content = parsed.find("content");
    if (content == parsed.end() || !content->is_array()) {
        return std::nullopt;
    }
    AnthropicReply reply;
    for (const nlohmann::json& block : *content) {
        if (block.is_object() && block.value("type", "") == "text") {
            reply.text += block.value("text", "");
        }
    }
    if (parsed.contains("stop_reason") && parsed["stop_reason"].is_string()) {
        reply.stopReason = parsed["stop_reason"].get<std::string>();
    }
    return reply;
}

std::optional<std::vector<std::string>> anthropicTranslations(std::string_view text) {
    const nlohmann::json parsed = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_object()) {
        const auto translations = parsed.find("translations");
        if (translations == parsed.end() || !translations->is_array()) {
            return std::nullopt;
        }
        std::vector<std::string> out;
        for (const nlohmann::json& one : *translations) {
            if (!one.is_string()) {
                return std::nullopt;
            }
            out.push_back(one.get<std::string>());
        }
        return out;
    }
    return core::parseJsonArray(text);
}

std::string buildMessagesRequest(const AnthropicTranslator::Options& options,
                                 const core::TranslateRequest& request,
                                 std::span<const std::string> segments, bool asJsonArray) {
    nlohmann::json body;
    body["model"] = options.model;
    body["max_tokens"] = options.maxTokens;
    body["system"] = llmSystemPrompt(request, asJsonArray);
    nlohmann::json messages = nlohmann::json::array();
    if (const std::optional<LlmTurn> context =
            asJsonArray ? llmContextTurn(request) : std::nullopt) {
        messages.push_back({{"role", "user"}, {"content", context->user}});
        messages.push_back({{"role", "assistant"}, {"content", context->assistant}});
    }
    messages.push_back(
        {{"role", "user"}, {"content", llmUserMessage(request, segments, asJsonArray)}});
    body["messages"] = std::move(messages);
    nlohmann::json config;
    if (!options.effort.empty()) {
        config["effort"] = options.effort;
    }
    if (asJsonArray) {
        config["format"] = {{"type", "json_schema"}, {"schema", translationsSchema()}};
    }
    if (!config.empty()) {
        body["output_config"] = std::move(config);
    }
    body["fallbacks"] = "default";
    body["stream"] = options.stream;
    return body.dump();
}

AnthropicTranslator::AnthropicTranslator(std::shared_ptr<IHttpClient> http, Options options)
    : http_(std::move(http)), options_(std::move(options)) {}

std::string AnthropicTranslator::send(const std::string& body, std::stop_token cancel,
                                      bool reportSegments) {
    std::string base = options_.baseUrl;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    HttpRequest http;
    http.url = base + "/v1/messages";
    http.method = HttpMethod::Post;
    http.body = body;
    http.timeout = options_.timeout;
    http.headers.emplace_back("Content-Type", "application/json");
    http.headers.emplace_back("x-api-key", options_.apiKey);
    http.headers.emplace_back("anthropic-version", "2023-06-01");
    // 伺服器端的備援（body 裡的 fallbacks）需要這個 beta
    http.headers.emplace_back("anthropic-beta", "server-side-fallback-2026-07-01");

    std::string content;
    std::string stopReason;
    std::string streamError;
    std::string pending;
    std::string received;  // 串流收到的全部原文（失敗時用來找錯誤說明）
    std::size_t reportedSegments = 0;
    if (options_.stream) {
        http.headers.emplace_back("Accept", "text/event-stream");
        http.onChunk = [&](std::string_view chunk) {
            pending.append(chunk);
            received.append(chunk);
            for (const std::string& data : takeSseData(pending)) {
                AnthropicEvent event = parseAnthropicEvent(data);
                content += event.text;
                if (!event.stopReason.empty()) {
                    stopReason = std::move(event.stopReason);
                }
                if (!event.error.empty()) {
                    streamError = std::move(event.error);
                }
            }
            if (reportSegments && onSegment_) {
                // {"translations": [...]}：從陣列開始的地方解析，收完一段就先顯示一段
                const std::size_t array = content.find('[');
                if (array != std::string::npos) {
                    const std::vector<std::string> ready =
                        core::parseJsonArrayPrefix(std::string_view(content).substr(array));
                    for (; reportedSegments < ready.size(); ++reportedSegments) {
                        onSegment_(reportedSegments, ready[reportedSegments]);
                    }
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
        if (!streamError.empty()) {
            // 串流開始之後才出錯（例如服務過載），換下一個引擎或稍後再試
            throw TranslatorError(TranslateError::Network, streamError);
        }
        checkStopReason(stopReason);
        return content;
    }
    std::optional<AnthropicReply> reply = parseAnthropicReply(response.body);
    if (!reply) {
        throw TranslatorError(TranslateError::BadResponse, "看不懂 API 的回應");
    }
    checkStopReason(reply->stopReason);
    return std::move(reply->text);
}

std::vector<std::string> AnthropicTranslator::translate(std::span<const std::string> segments,
                                                        const core::TranslateRequest& request,
                                                        std::stop_token cancel) {
    const core::BatchTranslate batch =
        [&](std::span<const std::string> part) -> std::vector<std::string> {
        // 只剩一段時不要 JSON，直接要譯文（design.md 4.5 的第三層退路）
        if (part.size() == 1) {
            return {send(buildMessagesRequest(options_, request, part, /*asJsonArray=*/false),
                         cancel, /*reportSegments=*/false)};
        }
        const std::string reply =
            send(buildMessagesRequest(options_, request, part, /*asJsonArray=*/true), cancel,
                 /*reportSegments=*/true);
        std::optional<std::vector<std::string>> parsed = anthropicTranslations(reply);
        if (!parsed) {
            throw TranslatorError(TranslateError::BadResponse, "回應不是譯文的 JSON");
        }
        return std::move(*parsed);
    };
    return core::translateAligned(segments, batch);
}

}  // namespace tmw::net
