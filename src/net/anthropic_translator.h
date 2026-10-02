// Claude 的原生 API（Anthropic Messages API）翻譯引擎（M2-07，design.md 4.5）。
//
// 提示詞和 OpenAI 相容格式的引擎完全一樣（net/llm_prompt），差別只在 API 本身：
// - 整批翻譯用 structured outputs（output_config.format）保證回傳的是 JSON，不靠提示詞要求。
// - output_config.effort 預設 low：翻譯不需要長考，越快越好。
// - 開啟伺服器端的備援（fallbacks: "default"）：模型的安全分類拒答時，伺服器依原因改用
//   其他模型，不必在這裡維護模型清單。真的被拒絕時 stop_reason 是 refusal，當成 Rejected。
// - 回應裡可能有思考區塊，只取文字區塊。
// C++ 沒有官方 SDK，直接用 HTTP。
#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "core/translator.h"
#include "net/http_client.h"

namespace tmw::net {

class AnthropicTranslator final : public core::ITranslator {
public:
    struct Options {
        std::string id = "anthropic";
        std::string baseUrl = "https://api.anthropic.com";  // 結尾不用加 /v1/messages
        std::string model = "claude-opus-5";
        std::string apiKey;
        // 回覆的長度上限（思考也算在裡面）。一個透鏡範圍的文字遠用不到。
        int maxTokens = 8192;
        std::string effort = "low";
        bool stream = true;
        std::chrono::milliseconds timeout{60000};
    };

    // 串流時每收完一段譯文就呼叫一次（和 OpenAiTranslator::OnSegment 相同）
    using OnSegment = std::function<void(std::size_t index, const std::string& text)>;

    AnthropicTranslator(std::shared_ptr<IHttpClient> http, Options options);

    std::string id() const override { return options_.id; }
    bool supportsBatch() const override { return true; }

    std::vector<std::string> translate(std::span<const std::string> segments,
                                       const core::TranslateRequest& request,
                                       std::stop_token cancel) override;

    void setOnSegment(OnSegment callback) { onSegment_ = std::move(callback); }

private:
    std::string send(const std::string& body, std::stop_token cancel, bool reportSegments);

    std::shared_ptr<IHttpClient> http_;
    Options options_;
    OnSegment onSegment_;
};

// 一則串流事件帶來的東西：新增的文字（content_block_delta 的 text_delta）、
// 結束原因（message_delta 的 stop_reason）、或錯誤（error 事件）。思考的內容一律略過。
struct AnthropicEvent {
    std::string text;
    std::string stopReason;
    std::string error;
};
AnthropicEvent parseAnthropicEvent(std::string_view data);

// 一次完整的回應：所有文字區塊接起來，以及結束原因。格式不符時回傳 nullopt。
struct AnthropicReply {
    std::string text;
    std::string stopReason;
};
std::optional<AnthropicReply> parseAnthropicReply(std::string_view body);

// structured outputs 的回覆 {"translations": [...]} 取出譯文。也接受直接的陣列。
std::optional<std::vector<std::string>> anthropicTranslations(std::string_view text);

// 送出去的請求內容。抽出來是為了測試，不必真的連線。
std::string buildMessagesRequest(const AnthropicTranslator::Options& options,
                                 const core::TranslateRequest& request,
                                 std::span<const std::string> segments, bool asJsonArray);

}  // namespace tmw::net
