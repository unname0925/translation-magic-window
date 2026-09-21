// CT-02：用真的向本機 LLM（Ollama 的 hy-mt2，OpenAI 相容格式）要過一次、錄下來的回應測試。
#include "net/openai_translator.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "net/sse.h"
#include "support/fake_http_client.h"

namespace tmw::net {
namespace {

using Strings = std::vector<std::string>;
using core::TranslateError;
using core::TranslateRequest;
using core::TranslatorError;

class OpenAiTranslatorTest : public ::testing::Test {
protected:
    std::shared_ptr<test::FakeHttpClient> http_ = std::make_shared<test::FakeHttpClient>();

    OpenAiTranslator makeTranslator(OpenAiTranslator::Options options = {}) {
        options.baseUrl = "http://127.0.0.1:11434/v1";
        options.model = "hy-mt2";
        return OpenAiTranslator(http_, std::move(options));
    }

    Strings translate(OpenAiTranslator& translator, const Strings& segments) {
        return translator.translate(segments, TranslateRequest{"ja", "zh-TW", {}, {}},
                                    std::stop_token{});
    }
};

TEST_F(OpenAiTranslatorTest, TranslatesAStreamedBatch) {
    http_->replyFromFile("net/openai_ja_batch_stream.txt");
    OpenAiTranslator translator = makeTranslator();
    const Strings out = translate(translator, {"こんにちは", "本気で戦うぞ", "セーブ"});
    EXPECT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0], "你好");
    EXPECT_EQ(http_->requests.size(), 1u) << "一個畫面的段落一次送出";
}

TEST_F(OpenAiTranslatorTest, TranslatesWithoutStreaming) {
    http_->replyFromFile("net/openai_ja_batch.json");
    OpenAiTranslator::Options options;
    options.stream = false;
    OpenAiTranslator translator = makeTranslator(std::move(options));
    const Strings out = translate(translator, {"こんにちは", "本気で戦うぞ", "セーブ"});
    EXPECT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0], "你好");
}

TEST_F(OpenAiTranslatorTest, ReportsEachSegmentWhileItStreams) {
    // 結果視窗要邊翻邊顯示（design.md 4.6），不能等整批收完
    http_->replyFromFile("net/openai_ja_batch_stream.txt");
    OpenAiTranslator translator = makeTranslator();
    std::vector<std::pair<std::size_t, std::string>> reported;
    translator.setOnSegment([&reported](std::size_t index, const std::string& text) {
        reported.emplace_back(index, text);
    });

    const Strings out = translate(translator, {"こんにちは", "本気で戦うぞ", "セーブ"});
    ASSERT_EQ(reported.size(), out.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        EXPECT_EQ(reported[i].first, i);
        EXPECT_EQ(reported[i].second, out[i]);
    }
}

TEST_F(OpenAiTranslatorTest, PutsThePromptAndTheSegmentsInTheRequest) {
    http_->replyFromFile("net/openai_ja_batch.json");
    OpenAiTranslator::Options options;
    options.stream = false;
    OpenAiTranslator translator = makeTranslator(std::move(options));
    translate(translator, {"こんにちは"});

    ASSERT_EQ(http_->requests.size(), 1u);
    const HttpRequest& sent = http_->requests[0];
    EXPECT_EQ(sent.url, "http://127.0.0.1:11434/v1/chat/completions");
    EXPECT_EQ(sent.method, HttpMethod::Post);
    EXPECT_NE(sent.body.find("hy-mt2"), std::string::npos);
    EXPECT_NE(sent.body.find("翻譯引擎"), std::string::npos);
}

TEST_F(OpenAiTranslatorTest, SendsTheKeyOnlyWhenThereIsOne) {
    http_->replyFromFile("net/openai_ja_batch.json");
    http_->replyFromFile("net/openai_ja_batch.json");

    OpenAiTranslator::Options local;
    local.stream = false;
    OpenAiTranslator withoutKey = makeTranslator(std::move(local));
    translate(withoutKey, {"こんにちは"});
    for (const auto& [name, value] : http_->requests[0].headers) {
        EXPECT_NE(name, "Authorization") << "本機服務不需要金鑰";
    }

    OpenAiTranslator::Options cloud;
    cloud.stream = false;
    cloud.apiKey = "sk-test";
    OpenAiTranslator withKey = makeTranslator(std::move(cloud));
    translate(withKey, {"こんにちは"});
    bool found = false;
    for (const auto& [name, value] : http_->requests[1].headers) {
        if (name == "Authorization") {
            found = true;
            EXPECT_EQ(value, "Bearer sk-test");
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(OpenAiTranslatorTest, UsesIpv4ForLocalServices) {
    // localhost 在 Windows 上會先試 IPv6，每個請求多約 2 秒（M0-12）
    http_->replyFromFile("net/openai_ja_batch.json");
    OpenAiTranslator::Options options;
    options.stream = false;
    options.baseUrl = "http://localhost:11434/v1/";
    OpenAiTranslator translator(http_, std::move(options));
    translator.translate(Strings{"こんにちは"}, TranslateRequest{"ja", "zh-TW", {}, {}},
                         std::stop_token{});
    EXPECT_EQ(http_->requests[0].url, "http://127.0.0.1:11434/v1/chat/completions")
        << "結尾多餘的斜線也要去掉";
}

TEST_F(OpenAiTranslatorTest, ResendsOneByOneWithoutJsonWhenTheArrayIsBroken) {
    // 模型偶爾會在譯文裡用沒有逸出的引號，JSON 就壞了（M0-12）
    const std::string broken =
        R"({"choices":[{"message":{"content":"[\"他說\"走吧\"\", \"好\"]"}}]})";
    OpenAiTranslator::Options options;
    options.stream = false;
    OpenAiTranslator translator = makeTranslator(std::move(options));
    http_->reply(broken);
    http_->reply(broken);
    http_->reply(R"({"choices":[{"message":{"content":"他說「走吧」"}}]})");
    http_->reply(R"({"choices":[{"message":{"content":"好"}}]})");

    EXPECT_EQ(translate(translator, {"「行くぞ」と言った", "よし"}),
              (Strings{"他說「走吧」", "好"}));
    ASSERT_EQ(http_->requests.size(), 4u) << "整批兩次，再逐段兩次";
    EXPECT_EQ(http_->requests[3].body.find("segments"), std::string::npos)
        << "逐段重送時不要 JSON，只要譯文";
}

TEST_F(OpenAiTranslatorTest, ReportsQuotaAndServerProblems) {
    OpenAiTranslator::Options options;
    options.stream = false;
    OpenAiTranslator translator = makeTranslator(std::move(options));

    http_->reply(R"({"error":{"message":"rate limit"}})", 429);
    try {
        translate(translator, {"こんにちは"});
        FAIL() << "429 應該丟例外";
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::RateLimited);
        EXPECT_NE(std::string(error.what()).find("rate limit"), std::string::npos);
    }

    http_->failToConnect("連線被拒絕");
    try {
        translate(translator, {"こんにちは"});
        FAIL() << "連不上應該丟例外";
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::Network);
    }
}

TEST_F(OpenAiTranslatorTest, DoesNotPutTheReplyIntoTheErrorMessage) {
    // 記錄檔不該出現截圖裡的文字，錯誤訊息只留狀態碼和 API 自己的說明
    OpenAiTranslator::Options options;
    options.stream = false;
    OpenAiTranslator translator = makeTranslator(std::move(options));
    http_->reply(R"({"choices":[{"message":{"content":"祕密的原文"}}], "x":1})", 400);
    try {
        translate(translator, {"こんにちは"});
        FAIL() << "400 應該丟例外";
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::Rejected)
            << "金鑰或模型名稱錯誤時重送同樣的內容不會變好";
        EXPECT_EQ(std::string(error.what()), "HTTP 400");
    }
    EXPECT_EQ(http_->requests.size(), 1u) << "被拒絕就不要再送一次";
}

TEST(StreamedContentTest, TakesTheDeltaText) {
    EXPECT_EQ(streamedContent(R"({"choices":[{"delta":{"content":"你好"}}]})"), "你好");
    EXPECT_EQ(streamedContent(R"({"choices":[{"delta":{"role":"assistant"}}]})"), "");
    EXPECT_EQ(streamedContent(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})"), "");
    EXPECT_EQ(streamedContent(R"({"usage":{"total_tokens":10}})"), "");
    EXPECT_EQ(streamedContent("[DONE]"), "");
    EXPECT_EQ(streamedContent(""), "");
}

TEST(CompletionContentTest, TakesTheMessageText) {
    EXPECT_EQ(completionContent(R"({"choices":[{"message":{"content":"你好"}}]})").value(), "你好");
    EXPECT_FALSE(completionContent(R"({"choices":[]})").has_value());
    EXPECT_FALSE(completionContent(R"({"error":{"message":"nope"}})").has_value());
    EXPECT_FALSE(completionContent("<html>").has_value());
}

TEST(BuildChatRequestTest, IncludesContextAndGlossaryOnlyWhenTheyExist) {
    const OpenAiTranslator::Options options;
    core::TranslateRequest request{"ja", "zh-TW", {}, {}};
    const Strings segments{"こんにちは"};

    const std::string bare = buildChatRequest(options, request, segments, true);
    EXPECT_EQ(bare.find("context"), std::string::npos);
    EXPECT_EQ(bare.find("glossary"), std::string::npos);

    request.context.emplace_back("前の台詞", "上一句");
    request.glossary["セーブ"] = "存檔";
    const std::string full = buildChatRequest(options, request, segments, true);
    EXPECT_NE(full.find("上一句"), std::string::npos);
    EXPECT_NE(full.find("存檔"), std::string::npos);
}

TEST(SseTest, AssemblesLinesSplitAcrossChunks) {
    std::string buffer;
    buffer += "data: {\"a\":";
    EXPECT_TRUE(takeSseData(buffer).empty()) << "這一行還沒收完";
    buffer += "1}\n\ndata: {\"b\":2}\n";
    EXPECT_EQ(takeSseData(buffer), (Strings{R"({"a":1})", R"({"b":2})"}));
    EXPECT_TRUE(buffer.empty());
}

TEST(SseTest, SkipsEverythingThatIsNotData) {
    std::string buffer = ": keep-alive\nevent: message\ndata: {\"a\":1}\r\ndata: [DONE]\n\n";
    EXPECT_EQ(takeSseData(buffer), (Strings{R"({"a":1})"}));
}

TEST(SseTest, KeepsThePartialLineForTheNextChunk) {
    std::string buffer = "data: {\"a\":1}\ndata: {\"b\"";
    EXPECT_EQ(takeSseData(buffer), (Strings{R"({"a":1})"}));
    EXPECT_EQ(buffer, "data: {\"b\"");
}

}  // namespace
}  // namespace tmw::net
