// CT-04：DeepL、Microsoft Translator、Google Cloud Translation（M2-07）。
// 回應依各家官方文件記載的格式手寫；沒有金鑰，還沒有對真的服務錄過回應。
#include "net/service_translators.h"

#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "support/fake_http_client.h"

namespace tmw::net {
namespace {

using Strings = std::vector<std::string>;
using core::TranslateError;
using core::TranslateRequest;
using core::TranslatorError;
using json = nlohmann::json;

std::string header(const HttpRequest& request, const std::string& name) {
    for (const auto& [key, value] : request.headers) {
        if (key == name) {
            return value;
        }
    }
    return {};
}

ServiceOptions options(const std::string& key) {
    ServiceOptions out;
    out.apiKey = key;
    return out;
}

TranslateRequest japanese() {
    return TranslateRequest{"ja", "zh-TW", {}, {}};
}

TranslateError failureOf(core::ITranslator& translator, const Strings& segments) {
    try {
        translator.translate(segments, japanese(), std::stop_token{});
    } catch (const TranslatorError& error) {
        return error.kind();
    }
    ADD_FAILURE() << "應該要失敗";
    return TranslateError::Unavailable;
}

// --- DeepL -------------------------------------------------------------------------------------

TEST(DeepLTest, BuildsTheDocumentedRequest) {
    auto http = std::make_shared<test::FakeHttpClient>();
    DeepLTranslator deepl(http, options("key-123"));
    TranslateRequest request = japanese();
    request.context = {{"前の台詞", "上一句"}};
    const HttpRequest sent = deepl.buildRequest(Strings{"こんにちは", "セーブ"}, request);
    EXPECT_EQ(sent.url, "https://api.deepl.com/v2/translate");
    EXPECT_EQ(header(sent, "Authorization"), "DeepL-Auth-Key key-123");
    const json body = json::parse(sent.body);
    EXPECT_EQ(body["text"], (Strings{"こんにちは", "セーブ"}));
    EXPECT_EQ(body["target_lang"], "ZH-HANT");
    EXPECT_EQ(body["source_lang"], "JA");
    EXPECT_EQ(body["context"], "前の台詞") << "前文只當參考，不翻譯也不計費";
}

TEST(DeepLTest, FreeKeysUseTheFreeEndpoint) {
    auto http = std::make_shared<test::FakeHttpClient>();
    DeepLTranslator deepl(http, options("abc:fx"));
    EXPECT_EQ(deepl.buildRequest(Strings{"a"}, japanese()).url,
              "https://api-free.deepl.com/v2/translate");
}

TEST(DeepLTest, AutoLanguageLeavesTheSourceOut) {
    auto http = std::make_shared<test::FakeHttpClient>();
    DeepLTranslator deepl(http, options("k"));
    const json body = json::parse(
        deepl.buildRequest(Strings{"a"}, TranslateRequest{"auto", "zh-TW", {}, {}}).body);
    EXPECT_FALSE(body.contains("source_lang"));
}

TEST(DeepLTest, TranslatesAndKeepsTheAuthorsReading) {
    auto http = std::make_shared<test::FakeHttpClient>();
    http->reply(R"({"translations":[{"detected_source_language":"JA","text":"我會認真戰鬥"},
                                     {"detected_source_language":"JA","text":"認真"},
                                     {"detected_source_language":"JA","text":"真的"}]})");
    DeepLTranslator deepl(http, options("k"));
    const Strings out = deepl.translate(Strings{"{本気|マジ}で戦うぞ"}, japanese(), {});
    EXPECT_EQ(out, (Strings{"我會認真戰鬥　［本気（マジ）→ 認真（真的）］"}));
    EXPECT_EQ(json::parse(http->requests.at(0).body)["text"],
              (Strings{"本気で戦うぞ", "本気", "マジ"}))
        << "標記拿掉，本文和讀音跟著同一個請求送出";
}

TEST(DeepLTest, AQuotaRunOutIsRateLimited) {
    auto http = std::make_shared<test::FakeHttpClient>();
    http->reply(R"({"message":"Quota exceeded"})", 456);
    DeepLTranslator deepl(http, options("k"));
    EXPECT_EQ(failureOf(deepl, {"こんにちは"}), TranslateError::RateLimited);
}

TEST(DeepLTest, ABadKeyIsRejectedWithDeepLsReason) {
    // 實測（2026-10-02，假金鑰）：DeepL 把原因放在最上層的 message，不在 error.message
    auto http = std::make_shared<test::FakeHttpClient>();
    http->reply(
        R"({"message":"Forbidden. You can find more info in our docs: https://developers.deepl.com/docs/getting-started/auth"})",
        403);
    DeepLTranslator deepl(http, options("k"));
    try {
        deepl.translate(Strings{"a"}, japanese(), {});
        FAIL();
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::Rejected);
        EXPECT_NE(std::string(error.what()).find("Forbidden"), std::string::npos) << error.what();
    }
}

TEST(DeepLTest, SplitsBigBatches) {
    // 一次最多 50 段
    auto http = std::make_shared<test::FakeHttpClient>();
    const auto reply = [&](std::size_t count) {
        json translations = json::array();
        for (std::size_t i = 0; i < count; ++i) {
            translations.push_back({{"text", "譯" + std::to_string(i)}});
        }
        http->reply(json{{"translations", translations}}.dump());
    };
    reply(50);
    reply(10);
    DeepLTranslator deepl(http, options("k"));
    const Strings out = deepl.translate(Strings(60, "あ"), japanese(), {});
    EXPECT_EQ(out.size(), 60u);
    EXPECT_EQ(http->requests.size(), 2u);
}

// --- Microsoft Translator ---------------------------------------------------------------------

TEST(AzureTest, BuildsTheDocumentedRequest) {
    auto http = std::make_shared<test::FakeHttpClient>();
    ServiceOptions opts = options("azure-key");
    opts.region = "eastasia";
    AzureTranslator azure(http, opts);
    const HttpRequest sent = azure.buildRequest(Strings{"こんにちは"}, japanese());
    EXPECT_EQ(sent.url,
              "https://api.cognitive.microsofttranslator.com/translate?api-version=3.0&to=zh-Hant"
              "&from=ja");
    EXPECT_EQ(header(sent, "Ocp-Apim-Subscription-Key"), "azure-key");
    EXPECT_EQ(header(sent, "Ocp-Apim-Subscription-Region"), "eastasia");
    EXPECT_EQ(json::parse(sent.body), json::parse(R"([{"Text":"こんにちは"}])"));
}

TEST(AzureTest, AGlobalResourceSendsNoRegion) {
    auto http = std::make_shared<test::FakeHttpClient>();
    AzureTranslator azure(http, options("k"));
    EXPECT_EQ(header(azure.buildRequest(Strings{"a"}, japanese()), "Ocp-Apim-Subscription-Region"),
              "");
}

TEST(AzureTest, ReadsTheDocumentedReply) {
    auto http = std::make_shared<test::FakeHttpClient>();
    http->reply(R"([{"translations":[{"text":"你好","to":"zh-Hant"}]},
                    {"translations":[{"text":"存檔","to":"zh-Hant"}]}])");
    AzureTranslator azure(http, options("k"));
    EXPECT_EQ(azure.translate(Strings{"こんにちは", "セーブ"}, japanese(), {}),
              (Strings{"你好", "存檔"}));
}

TEST(AzureTest, ReportsTheServicesReason) {
    auto http = std::make_shared<test::FakeHttpClient>();
    http->reply(R"({"error":{"code":401000,"message":"The request is not authorized"}})", 401);
    AzureTranslator azure(http, options("k"));
    try {
        azure.translate(Strings{"a"}, japanese(), {});
        FAIL();
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::Rejected);
        EXPECT_NE(std::string(error.what()).find("not authorized"), std::string::npos);
    }
}

// --- Google Cloud Translation ------------------------------------------------------------------

TEST(GoogleCloudTest, BuildsTheDocumentedRequestWithTheKeyInAHeader) {
    auto http = std::make_shared<test::FakeHttpClient>();
    GoogleCloudTranslator google(http, options("gc-key"));
    const HttpRequest sent = google.buildRequest(Strings{"こんにちは"}, japanese());
    EXPECT_EQ(sent.url, "https://translation.googleapis.com/language/translate/v2");
    EXPECT_EQ(sent.url.find("gc-key"), std::string::npos) << "金鑰不放在網址裡";
    EXPECT_EQ(header(sent, "X-Goog-Api-Key"), "gc-key");
    const json body = json::parse(sent.body);
    EXPECT_EQ(body["q"], (Strings{"こんにちは"}));
    EXPECT_EQ(body["target"], "zh-TW");
    EXPECT_EQ(body["source"], "ja");
    EXPECT_EQ(body["format"], "text");
}

TEST(GoogleCloudTest, ReadsTheDocumentedReply) {
    auto http = std::make_shared<test::FakeHttpClient>();
    http->reply(
        R"({"data":{"translations":[{"translatedText":"你好"},{"translatedText":"存檔"}]}})");
    GoogleCloudTranslator google(http, options("k"));
    EXPECT_EQ(google.translate(Strings{"こんにちは", "セーブ"}, japanese(), {}),
              (Strings{"你好", "存檔"}));
}

TEST(GoogleCloudTest, AWrongCountIsABadResponse) {
    auto http = std::make_shared<test::FakeHttpClient>();
    http->reply(R"({"data":{"translations":[{"translatedText":"你好"}]}})");
    GoogleCloudTranslator google(http, options("k"));
    EXPECT_EQ(failureOf(google, {"こんにちは", "セーブ"}), TranslateError::BadResponse);
}

}  // namespace
}  // namespace tmw::net
