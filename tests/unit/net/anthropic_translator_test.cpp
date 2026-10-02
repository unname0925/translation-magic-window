// CT-03：Claude 原生 API（Anthropic Messages API）的翻譯引擎（M2-07）。
// 回應依官方文件記載的格式手寫；錄下來的真實回應另外放在 tests/data/net/anthropic_*。
#include "net/anthropic_translator.h"

#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "net/llm_prompt.h"
#include "support/fake_http_client.h"

namespace tmw::net {
namespace {

using Strings = std::vector<std::string>;
using core::TranslateError;
using core::TranslateRequest;
using core::TranslatorError;

// 一則 SSE 事件
std::string event(const std::string& type, const nlohmann::json& data) {
    return "event: " + type + "\ndata: " + data.dump() + "\n\n";
}

std::string textDelta(const std::string& text) {
    return event("content_block_delta", {{"type", "content_block_delta"},
                                         {"index", 1},
                                         {"delta", {{"type", "text_delta"}, {"text", text}}}});
}

std::string stop(const std::string& reason) {
    return event("message_delta",
                 {{"type", "message_delta"}, {"delta", {{"stop_reason", reason}}}}) +
           event("message_stop", {{"type", "message_stop"}});
}

// 先有一段思考（不能混進譯文），再分好幾塊送出 {"translations": [...]}
std::string streamedBatch() {
    return event("message_start", {{"type", "message_start"}, {"message", {{"id", "msg_1"}}}}) +
           event("content_block_delta",
                 {{"type", "content_block_delta"},
                  {"index", 0},
                  {"delta", {{"type", "thinking_delta"}, {"thinking", "想一下"}}}}) +
           textDelta(R"({"translations": ["你)") + textDelta(R"(好", "我會認)") +
           textDelta(R"(真戰鬥"]})") + stop("end_turn");
}

class AnthropicTranslatorTest : public ::testing::Test {
protected:
    std::shared_ptr<test::FakeHttpClient> http_ = std::make_shared<test::FakeHttpClient>();

    AnthropicTranslator makeTranslator(AnthropicTranslator::Options options = {}) {
        options.apiKey = "sk-ant-test";
        return AnthropicTranslator(http_, std::move(options));
    }

    Strings translate(AnthropicTranslator& translator, const Strings& segments) {
        return translator.translate(segments, TranslateRequest{"ja", "zh-TW", {}, {}},
                                    std::stop_token{});
    }

    TranslateError failureOf(AnthropicTranslator& translator, const Strings& segments) {
        try {
            translate(translator, segments);
        } catch (const TranslatorError& error) {
            return error.kind();
        }
        ADD_FAILURE() << "應該要失敗";
        return TranslateError::Unavailable;
    }
};

TEST_F(AnthropicTranslatorTest, TranslatesAStreamedBatchAndSkipsThinking) {
    http_->reply(streamedBatch());
    AnthropicTranslator translator = makeTranslator();
    std::vector<std::pair<std::size_t, std::string>> seen;
    translator.setOnSegment(
        [&](std::size_t index, const std::string& text) { seen.emplace_back(index, text); });
    const Strings out = translate(translator, {"こんにちは", "本気で戦うぞ"});
    EXPECT_EQ(out, (Strings{"你好", "我會認真戰鬥"}));
    ASSERT_EQ(seen.size(), 2u) << "收完一段就先顯示一段";
    EXPECT_EQ(seen[0].second, "你好");
    ASSERT_EQ(http_->requests.size(), 1u);
}

TEST_F(AnthropicTranslatorTest, SendsWhatTheApiNeeds) {
    http_->reply(streamedBatch());
    AnthropicTranslator translator = makeTranslator();
    translate(translator, {"こんにちは", "本気で戦うぞ"});
    const HttpRequest& sent = http_->requests.at(0);
    EXPECT_EQ(sent.url, "https://api.anthropic.com/v1/messages");
    const auto header = [&](const std::string& name) {
        for (const auto& [key, value] : sent.headers) {
            if (key == name) {
                return value;
            }
        }
        return std::string();
    };
    EXPECT_EQ(header("x-api-key"), "sk-ant-test");
    EXPECT_EQ(header("anthropic-version"), "2023-06-01");
    EXPECT_EQ(header("anthropic-beta"), "server-side-fallback-2026-07-01");
}

TEST_F(AnthropicTranslatorTest, TheBatchRequestUsesStructuredOutputs) {
    const AnthropicTranslator::Options options;
    const TranslateRequest request{"ja", "zh-TW", {}, {}};
    const Strings segments{"こんにちは", "セーブ"};
    const nlohmann::json body =
        nlohmann::json::parse(buildMessagesRequest(options, request, segments, true));
    EXPECT_EQ(body["model"], "claude-opus-5");
    EXPECT_EQ(body["system"], llmSystemPrompt(request, true)) << "和 OpenAI 那支的提示詞相同";
    EXPECT_EQ(body["messages"][0]["content"], llmUserMessage(request, segments, true));
    EXPECT_EQ(body["output_config"]["effort"], "low");
    EXPECT_EQ(body["output_config"]["format"]["type"], "json_schema");
    EXPECT_EQ(body["fallbacks"], "default");
    EXPECT_FALSE(body.contains("temperature")) << "新模型不接受非預設的取樣參數";
    EXPECT_FALSE(body.contains("thinking")) << "思考交給 effort 控制";

    const nlohmann::json plain =
        nlohmann::json::parse(buildMessagesRequest(options, request, Strings{"セーブ"}, false));
    EXPECT_FALSE(plain["output_config"].contains("format")) << "單段時只要譯文本身";
    EXPECT_EQ(plain["messages"][0]["content"], "セーブ");
}

TEST_F(AnthropicTranslatorTest, TranslatesWithoutStreaming) {
    http_->reply(
        nlohmann::json{{"content",
                        {{{"type", "thinking"}, {"thinking", ""}},
                         {{"type", "text"}, {"text", R"({"translations": ["你好", "存檔"]})"}}}},
                       {"stop_reason", "end_turn"}}
            .dump());
    AnthropicTranslator::Options options;
    options.stream = false;
    AnthropicTranslator translator = makeTranslator(std::move(options));
    EXPECT_EQ(translate(translator, {"こんにちは", "セーブ"}), (Strings{"你好", "存檔"}));
}

TEST_F(AnthropicTranslatorTest, ARefusalIsRejected) {
    http_->reply(textDelta("") + stop("refusal"));
    AnthropicTranslator translator = makeTranslator();
    EXPECT_EQ(failureOf(translator, {"こんにちは"}), TranslateError::Rejected);
}

TEST_F(AnthropicTranslatorTest, ACutOffReplyIsNotUsed) {
    // 整批：被截斷、對齊不起來 → 重試 → 逐段（都被截斷）→ 最後一個錯誤
    for (int i = 0; i < 6; ++i) {
        http_->reply(textDelta(R"({"translations": ["你)") + stop("max_tokens"));
    }
    AnthropicTranslator translator = makeTranslator();
    EXPECT_EQ(failureOf(translator, {"こんにちは", "セーブ"}), TranslateError::BadResponse);
}

TEST_F(AnthropicTranslatorTest, AnOverloadedServiceIsANetworkProblem) {
    http_->reply(R"({"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}})",
                 529);
    AnthropicTranslator translator = makeTranslator();
    EXPECT_EQ(failureOf(translator, {"こんにちは"}), TranslateError::Network);
}

TEST_F(AnthropicTranslatorTest, ABadKeyIsRejectedWithTheApisReason) {
    http_->reply(
        R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}})",
        401);
    AnthropicTranslator translator = makeTranslator();
    try {
        translate(translator, {"こんにちは"});
        FAIL() << "金鑰錯誤應該失敗";
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::Rejected);
        EXPECT_NE(std::string(error.what()).find("invalid x-api-key"), std::string::npos);
    }
}

TEST_F(AnthropicTranslatorTest, AnErrorInTheMiddleOfTheStreamIsANetworkProblem) {
    http_->reply(
        textDelta(R"({"translations": [)") +
        event("error", {{"type", "error"},
                        {"error", {{"type", "overloaded_error"}, {"message", "Overloaded"}}}}));
    AnthropicTranslator translator = makeTranslator();
    EXPECT_EQ(failureOf(translator, {"こんにちは"}), TranslateError::Network);
}

TEST(AnthropicTranslationsTest, AcceptsTheObjectOrAPlainArray) {
    EXPECT_EQ(anthropicTranslations(R"({"translations": ["a", "b"]})"), (Strings{"a", "b"}));
    EXPECT_EQ(anthropicTranslations(R"(["a"])"), (Strings{"a"}));
    EXPECT_FALSE(anthropicTranslations(R"({"other": []})").has_value());
    EXPECT_FALSE(anthropicTranslations(R"({"translations": [1]})").has_value());
}

}  // namespace
}  // namespace tmw::net
