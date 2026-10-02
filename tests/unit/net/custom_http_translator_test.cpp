// 自訂 HTTP 範本引擎（M2-07）。例子用 LibreTranslate 的格式：
// POST /translate {"q": "...", "source": "ja", "target": "zh-TW", "api_key": "..."} →
// {"translatedText": "..."}
#include "net/custom_http_translator.h"

#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <stop_token>
#include <string>
#include <vector>

#include "support/fake_http_client.h"

namespace tmw::net {
namespace {

using Strings = std::vector<std::string>;
using core::TranslateError;
using core::TranslateRequest;
using core::TranslatorError;

CustomHttpOptions libreTranslate() {
    CustomHttpOptions options;
    options.url = "https://libretranslate.example/translate";
    options.headers = "Accept: application/json\nX-Client: tmw";
    options.bodyTemplate =
        R"({"q": "{{text}}", "source": "{{source}}", "target": "{{target}}", "api_key": "{{key}}"})";
    options.responsePath = "translatedText";
    options.apiKey = "secret";
    return options;
}

TranslateRequest japanese() {
    return TranslateRequest{"ja", "zh-TW", {}, {}};
}

TEST(CustomHttpTest, FillsTheTemplate) {
    auto http = std::make_shared<test::FakeHttpClient>();
    CustomHttpTranslator translator(http, libreTranslate());
    // 引號和換行要跳脫，不然範本組出來的 JSON 會壞掉
    const HttpRequest sent = translator.buildRequest("「本気」だ\n\"ok\"", japanese());
    EXPECT_EQ(sent.method, HttpMethod::Post);
    const nlohmann::json body = nlohmann::json::parse(sent.body);
    EXPECT_EQ(body["q"], "「本気」だ\n\"ok\"");
    EXPECT_EQ(body["source"], "ja");
    EXPECT_EQ(body["target"], "zh-TW");
    EXPECT_EQ(body["api_key"], "secret");
    ASSERT_EQ(sent.headers.size(), 3u) << "兩個自訂標頭，加上自動補的 Content-Type";
    EXPECT_EQ(sent.headers[0].first, "Accept");
    EXPECT_EQ(sent.headers[1].second, "tmw");
    EXPECT_EQ(sent.headers[2].first, "Content-Type");
}

TEST(CustomHttpTest, AnEmptyBodyMeansGetWithTheTextInTheUrl) {
    auto http = std::make_shared<test::FakeHttpClient>();
    CustomHttpOptions options;
    options.url = "https://example.test/t?q={{text}}&from={{source}}";
    options.responsePath = "result";
    CustomHttpTranslator translator(http, options);
    const HttpRequest sent = translator.buildRequest("あ b", TranslateRequest{"", "zh-TW", {}, {}});
    EXPECT_EQ(sent.method, HttpMethod::Get);
    EXPECT_EQ(sent.url, "https://example.test/t?q=%E3%81%82%20b&from=auto");
}

TEST(CustomHttpTest, TranslatesOneSegmentPerRequest) {
    auto http = std::make_shared<test::FakeHttpClient>();
    http->reply(R"({"translatedText": "你好"})");
    http->reply(R"({"translatedText": "存檔"})");
    CustomHttpTranslator translator(http, libreTranslate());
    EXPECT_EQ(translator.translate(Strings{"こんにちは", "セーブ"}, japanese(), {}),
              (Strings{"你好", "存檔"}));
    EXPECT_EQ(http->requests.size(), 2u);
}

TEST(CustomHttpTest, AWrongPathIsABadResponse) {
    auto http = std::make_shared<test::FakeHttpClient>();
    http->reply(R"({"other": "你好"})");
    CustomHttpTranslator translator(http, libreTranslate());
    try {
        translator.translate(Strings{"こんにちは"}, japanese(), {});
        FAIL();
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::BadResponse);
        EXPECT_NE(std::string(error.what()).find("translatedText"), std::string::npos);
    }
}

TEST(JsonPathTest, FollowsKeysAndIndexes) {
    EXPECT_EQ(extractJsonPath(R"({"translations":[{"text":"甲"},{"text":"乙"}]})",
                              "translations[1].text"),
              "乙");
    EXPECT_EQ(extractJsonPath(R"([{"translations":[{"text":"甲"}]}])", "[0].translations[0].text"),
              "甲");
    EXPECT_EQ(extractJsonPath(R"({"data":{"translatedText":"甲"}})", "data.translatedText"), "甲");
}

TEST(JsonPathTest, ReturnsNothingWhenThePathIsWrong) {
    EXPECT_FALSE(extractJsonPath(R"({"a":1})", "a").has_value()) << "不是字串";
    EXPECT_FALSE(extractJsonPath(R"({"a":["x"]})", "a[3]").has_value());
    EXPECT_FALSE(extractJsonPath(R"({"a":["x"]})", "a[x]").has_value());
    EXPECT_FALSE(extractJsonPath("not json", "a").has_value());
}

TEST(CustomHttpProblemTest, ChecksTheTemplateBeforeSaving) {
    EXPECT_EQ(customHttpProblem(libreTranslate()), "");
    CustomHttpOptions options = libreTranslate();
    options.url = "libretranslate.example";
    EXPECT_NE(customHttpProblem(options), "");
    options = libreTranslate();
    options.bodyTemplate = R"({"q": "hello"})";
    EXPECT_NE(customHttpProblem(options).find("{{text}}"), std::string::npos);
    options = libreTranslate();
    options.responsePath = " ";
    EXPECT_NE(customHttpProblem(options), "");
    options = libreTranslate();
    options.headers = "no colon here";
    EXPECT_NE(customHttpProblem(options).find("no colon here"), std::string::npos);
}

}  // namespace
}  // namespace tmw::net
