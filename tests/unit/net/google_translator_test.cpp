// CT-01：用真的向端點要過一次、錄下來的回應測試 Google 引擎。
#include "net/google_translator.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include "support/fake_clock.h"
#include "support/fake_http_client.h"

namespace tmw::net {
namespace {

using Strings = std::vector<std::string>;
using core::TranslateError;
using core::TranslateRequest;
using core::TranslatorError;

class GoogleTranslatorTest : public ::testing::Test {
protected:
    std::shared_ptr<test::FakeHttpClient> http_ = std::make_shared<test::FakeHttpClient>();
    test::FakeClock clock_;
    Duration slept_{};

    // 假的等待：不真的睡，直接把假時鐘往前撥
    Sleeper sleeper() {
        return [this](Duration wait, std::stop_token) {
            slept_ += wait;
            clock_.advance(wait);
        };
    }

    GoogleTranslator makeTranslator(GoogleTranslator::Options options = {}) {
        return GoogleTranslator(http_, clock_, std::move(options), sleeper());
    }

    Strings translate(GoogleTranslator& translator, const Strings& segments,
                      const std::string& language = "ja") {
        return translator.translate(segments, TranslateRequest{language, "zh-TW", {}, {}},
                                    std::stop_token{});
    }
};

TEST_F(GoogleTranslatorTest, TranslatesAWholeBatchInOneRequest) {
    http_->replyFromFile("net/google_ja_batch.json");
    GoogleTranslator translator = makeTranslator();
    const Strings out = translate(translator, {"こんにちは", "本気で戦うぞ", "セーブ"});
    EXPECT_EQ(out, (Strings{"你好", "我會認真戰鬥", "儲存"}));
    EXPECT_EQ(http_->requests.size(), 1u) << "一整批只送一個請求";
}

TEST_F(GoogleTranslatorTest, TranslatesEnglishAndKorean) {
    http_->replyFromFile("net/google_en_batch.json");
    GoogleTranslator translator = makeTranslator();
    const Strings english =
        translate(translator, {"THAT'S A PERK?", "Press [E] to open", "Save"}, "en");
    EXPECT_EQ(english.size(), 3u);
    EXPECT_EQ(english.back(), "儲存");

    http_->replyFromFile("net/google_ko_batch.json");
    const Strings korean = translate(translator, {"이 대통령", "저장", "계속하시겠습니까?"}, "ko");
    EXPECT_EQ(korean.size(), 3u);
    EXPECT_EQ(korean[1], "儲存");
}

TEST_F(GoogleTranslatorTest, PutsEverythingTheEndpointNeedsInTheUrl) {
    http_->replyFromFile("net/google_ja_single.json");
    GoogleTranslator translator = makeTranslator();
    translate(translator, {"ロード"});

    ASSERT_EQ(http_->requests.size(), 1u);
    const HttpRequest& sent = http_->requests[0];
    EXPECT_NE(sent.url.find("client=gtx"), std::string::npos);
    EXPECT_NE(sent.url.find("sl=ja"), std::string::npos);
    EXPECT_NE(sent.url.find("tl=zh-TW"), std::string::npos);
    EXPECT_NE(sent.url.find("dt=t"), std::string::npos);
    EXPECT_NE(sent.url.find("q=%E3%83%AD%E3%83%BC%E3%83%89"), std::string::npos) << "原文要編碼";
    ASSERT_FALSE(sent.headers.empty()) << "沒有 User-Agent 時端點會回 403";
    EXPECT_EQ(sent.headers[0].first, "User-Agent");
}

TEST_F(GoogleTranslatorTest, UnknownSourceLanguageIsLeftToTheEndpoint) {
    http_->replyFromFile("net/google_ja_single.json");
    GoogleTranslator translator = makeTranslator();
    translate(translator, {"ロード"}, "auto");
    EXPECT_NE(http_->requests[0].url.find("sl=auto"), std::string::npos);
}

TEST_F(GoogleTranslatorTest, ResendsOneByOneWhenTheCountDoesNotMatch) {
    // 端點偶爾會把換行吃掉，回來的段數就對不上
    http_->reply(R"([[["你好儲存","こんにちは\nセーブ",null,null,3]],null,"ja"])");
    http_->reply(R"([[["你好","こんにちは",null,null,3]],null,"ja"])");
    http_->reply(R"([[["儲存","セーブ",null,null,3]],null,"ja"])");

    GoogleTranslator translator = makeTranslator();
    EXPECT_EQ(translate(translator, {"こんにちは", "セーブ"}), (Strings{"你好", "儲存"}));
    EXPECT_EQ(http_->requests.size(), 3u) << "整批一次 + 逐段兩次，中間不重試整批";
}

TEST_F(GoogleTranslatorTest, FlattensNewlinesWhenResendingOneSegment) {
    // 逐段重送時，一段的譯文含有換行會讓數量又對不上
    http_->reply(R"([[["甲乙","x",null,null,3]],null,"ja"])");
    http_->reply(R"([[["甲\n乙","x",null,null,3]],null,"ja"])");
    http_->reply(R"([[["丙","y",null,null,3]],null,"ja"])");

    GoogleTranslator translator = makeTranslator();
    EXPECT_EQ(translate(translator, {"x", "y"}), (Strings{"甲 乙", "丙"}));
}

TEST_F(GoogleTranslatorTest, KeepsAtMostOneRequestPerSecond) {
    for (int i = 0; i < 3; ++i) {
        http_->replyFromFile("net/google_ja_single.json");
    }
    GoogleTranslator translator = makeTranslator();
    translate(translator, {"ロード"});
    EXPECT_EQ(slept_, Duration::zero()) << "第一個請求不必等";

    translate(translator, {"ロード"});
    EXPECT_EQ(slept_, std::chrono::seconds(1));

    clock_.advance(std::chrono::seconds(5));
    translate(translator, {"ロード"});
    EXPECT_EQ(slept_, std::chrono::seconds(1)) << "已經隔夠久就不用再等";
}

TEST_F(GoogleTranslatorTest, StripsRubyMarkupBeforeSending) {
    // 這個端點看不懂 {本文|讀音}：標記會被翻掉，對齊檢查就會一直判定格式錯誤。
    // 所以先還原成只有本文（design.md 4.5）。
    http_->reply(R"([[["我會認真戰鬥","x",null,null,3]],null,"ja"])");
    GoogleTranslator translator = makeTranslator();
    const Strings out = translate(translator, {"{本気|マジ}で戦うぞ"});

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], "我會認真戰鬥") << "譯文沒有標記也不算失敗";
    ASSERT_EQ(http_->requests.size(), 1u) << "不該因為標記對不上而重送";
    EXPECT_EQ(http_->requests[0].url.find("%7B"), std::string::npos) << "送出去的原文不含大括號";
}

TEST_F(GoogleTranslatorTest, ReportsRateLimiting) {
    http_->reply("", 429);
    GoogleTranslator translator = makeTranslator();
    try {
        translate(translator, {"こんにちは"});
        FAIL() << "429 應該丟例外";
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::RateLimited);
    }
}

TEST_F(GoogleTranslatorTest, ReportsConnectionFailures) {
    http_->failToConnect("無法解析主機名稱");
    GoogleTranslator translator = makeTranslator();
    try {
        translate(translator, {"こんにちは"});
        FAIL() << "連不上應該丟例外";
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::Network);
    }
}

TEST_F(GoogleTranslatorTest, ReportsServerErrorsAsNetworkProblems) {
    http_->reply("<html>502</html>", 502);
    GoogleTranslator translator = makeTranslator();
    try {
        translate(translator, {"こんにちは"});
        FAIL() << "502 應該丟例外";
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::Network);
    }
}

TEST_F(GoogleTranslatorTest, ReportsBeingBlocked) {
    // 少了 User-Agent 或送太快時端點會回 403，重送同樣的內容不會變好
    http_->reply("", 403);
    GoogleTranslator translator = makeTranslator();
    try {
        translate(translator, {"こんにちは"});
        FAIL() << "403 應該丟例外";
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::Rejected);
    }
    EXPECT_EQ(http_->requests.size(), 1u);
}

TEST_F(GoogleTranslatorTest, ReportsRepliesItCannotRead) {
    // 端點改格式或被中間的裝置換成登入頁面
    http_->reply("<html>請先登入</html>");
    http_->reply("<html>請先登入</html>");
    GoogleTranslator translator = makeTranslator();
    try {
        translate(translator, {"こんにちは"});
        FAIL() << "看不懂的回應應該丟例外";
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::BadResponse);
    }
}

TEST_F(GoogleTranslatorTest, StopsWhenCancelled) {
    std::stop_source source;
    source.request_stop();
    GoogleTranslator translator = makeTranslator();
    try {
        translator.translate(Strings{"こんにちは"}, TranslateRequest{"ja", "zh-TW", {}, {}},
                             source.get_token());
        FAIL() << "取消時應該丟例外";
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::Cancelled);
    }
    EXPECT_TRUE(http_->requests.empty()) << "已經取消就不要再送出去";
}

TEST(ParseGoogleResponseTest, ConcatenatesEveryPiece) {
    // 長一點的原文會被切成好幾段回來
    const auto parsed = parseGoogleResponse(
        R"([[["你好，","Hello, ",null,null,3],["世界","world",null,null,3]],null,"en"])");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, "你好，世界");
}

TEST(ParseGoogleResponseTest, HandlesEmptyAndBrokenReplies) {
    EXPECT_EQ(parseGoogleResponse(R"([null,null,"ja"])").value(), "");
    EXPECT_FALSE(parseGoogleResponse("").has_value());
    EXPECT_FALSE(parseGoogleResponse("<html>").has_value());
    EXPECT_FALSE(parseGoogleResponse("[]").has_value());
    EXPECT_FALSE(parseGoogleResponse(R"({"error": 1})").has_value());
}

TEST(GoogleUrlTest, EncodesEverythingThatIsNotUnreserved) {
    EXPECT_EQ(percentEncode("abc-_.~123"), "abc-_.~123");
    EXPECT_EQ(percentEncode("a b&c=d"), "a%20b%26c%3Dd");
    EXPECT_EQ(percentEncode("あ"), "%E3%81%82");
    EXPECT_EQ(percentEncode("\n"), "%0A");
}

TEST(GoogleUrlTest, BuildsAQueryString) {
    EXPECT_EQ(buildUrl("https://example.com/x", {{"a", "1"}, {"b", "2 3"}}),
              "https://example.com/x?a=1&b=2%203");
    EXPECT_EQ(buildUrl("https://example.com/x?k=v", {{"a", "1"}}), "https://example.com/x?k=v&a=1");
    EXPECT_EQ(buildUrl("https://example.com/x", {}), "https://example.com/x");
}

}  // namespace
}  // namespace tmw::net
