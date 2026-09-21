// UT-05：LLM 回應的各種異常（多一段、少一段、夾雜說明文字、code block、JSON 壞掉），
// 每一種都要被正確處理或觸發逐段重翻。
#include "core/translation_alignment.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace tmw::core {
namespace {

using Strings = std::vector<std::string>;

TEST(ParseJsonArrayTest, PlainArray) {
    const auto parsed = parseJsonArray(R"(["你好", "再見"])");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, (Strings{"你好", "再見"}));
}

TEST(ParseJsonArrayTest, CodeBlock) {
    const auto parsed = parseJsonArray("```json\n[\"你好\", \"再見\"]\n```");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, (Strings{"你好", "再見"}));
}

TEST(ParseJsonArrayTest, CodeBlockWithoutLanguage) {
    const auto parsed = parseJsonArray("```\n[\"你好\"]\n```");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, (Strings{"你好"}));
}

TEST(ParseJsonArrayTest, SurroundedByProse) {
    const auto parsed =
        parseJsonArray("好的，以下是翻譯：\n[\"你好\", \"再見\"]\n希望對你有幫助！");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, (Strings{"你好", "再見"}));
}

TEST(ParseJsonArrayTest, KeepsBracketsInsideStrings) {
    // 譯文本身含有中括號和跳脫過的引號時，不能把它當成陣列的結尾
    const auto parsed = parseJsonArray(R"(["[系統] 他說\"走吧\"", "好"])");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, (Strings{"[系統] 他說\"走吧\"", "好"}));
}

TEST(ParseJsonArrayTest, AnUnpairedBracketInsideAStringIsNotTheEnd) {
    // 遊戲介面常有「[系統]」這種標記，OCR 也可能只認出半邊；
    // 不追蹤字串的話，這個 ] 會被當成陣列結束，後面整段就不見了
    const auto parsed = parseJsonArray(R"(["結束]", "好"])");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, (Strings{"結束]", "好"}));
}

TEST(ParseJsonArrayTest, NumbersBecomeText) {
    // 原文是 "110" 這種純數字時，LLM 偶爾會回傳數字
    const auto parsed = parseJsonArray("[110, \"好\"]");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, (Strings{"110", "好"}));
}

TEST(ParseJsonArrayTest, BrokenJsonIsRejected) {
    EXPECT_FALSE(parseJsonArray(R"(["你好", "再見)").has_value()) << "少了收尾";
    EXPECT_FALSE(parseJsonArray(R"(["你好" "再見"])").has_value()) << "少了逗號";
    EXPECT_FALSE(parseJsonArray("抱歉，我無法翻譯這段文字。").has_value()) << "根本沒有陣列";
    EXPECT_FALSE(parseJsonArray("").has_value());
}

TEST(ParseJsonArrayTest, NestedStructuresAreRejected) {
    EXPECT_FALSE(parseJsonArray(R"([{"text": "你好"}])").has_value());
    EXPECT_FALSE(parseJsonArray(R"([["你好"], ["再見"]])").has_value());
}

TEST(SplitLinesTest, KeepsEmptyLinesInTheMiddle) {
    // 空行代表那一段翻出來是空的，不能丟掉，否則數量就對不上了
    EXPECT_EQ(splitLines("甲\n\n丙"), (Strings{"甲", "", "丙"}));
    EXPECT_EQ(splitLines("甲\r\n乙\r\n"), (Strings{"甲", "乙"}));
    EXPECT_EQ(splitLines("只有一段"), (Strings{"只有一段"}));
    EXPECT_EQ(splitLines(""), (Strings{""}));
}

TEST(RubyMarkerTest, CountsOnlyCompleteMarkers) {
    EXPECT_EQ(countRubyMarkers("{本気|マジ}で戦うぞ"), 1);
    EXPECT_EQ(countRubyMarkers("{未来|あした}と{強敵|とも}"), 2);
    EXPECT_EQ(countRubyMarkers("沒有標記"), 0);
    EXPECT_EQ(countRubyMarkers("{沒有直線}"), 0);
    EXPECT_EQ(countRubyMarkers("{被截斷|"), 0);
}

TEST(CheckAlignmentTest, AcceptsMatchingOutput) {
    const Strings sources{"こんにちは", "{本気|マジ}だ"};
    const Strings translations{"你好", "{認真|玩真的}"};
    EXPECT_EQ(checkAlignment(sources, translations), AlignmentProblem::None);
}

TEST(CheckAlignmentTest, RejectsWrongCount) {
    const Strings sources{"一", "二"};
    EXPECT_EQ(checkAlignment(sources, Strings{"1", "2", "3"}), AlignmentProblem::WrongCount);
    EXPECT_EQ(checkAlignment(sources, Strings{"1"}), AlignmentProblem::WrongCount);
}

TEST(CheckAlignmentTest, RejectsEmptyTranslationOfNonEmptySource) {
    EXPECT_EQ(checkAlignment(Strings{"こんにちは"}, Strings{""}), AlignmentProblem::EmptyText);
    EXPECT_EQ(checkAlignment(Strings{"こんにちは"}, Strings{"   "}), AlignmentProblem::EmptyText);
    EXPECT_EQ(checkAlignment(Strings{""}, Strings{""}), AlignmentProblem::None)
        << "原文本來就是空的";
}

TEST(CheckAlignmentTest, RejectsLostRubyMarkers) {
    EXPECT_EQ(checkAlignment(Strings{"{本気|マジ}だ"}, Strings{"我是認真的"}),
              AlignmentProblem::RubyMismatch);
}

// 假引擎：依序回傳預先準備好的結果，記下每一次收到幾段
class FakeBatch {
public:
    explicit FakeBatch(std::vector<Strings> replies) : replies_(std::move(replies)) {}

    Strings operator()(std::span<const std::string> sources) {
        calls.emplace_back(sources.begin(), sources.end());
        if (calls.size() > replies_.size()) {
            throw TranslatorError(TranslateError::BadResponse, "假引擎沒有準備這麼多回應");
        }
        return replies_[calls.size() - 1];
    }

    std::vector<Strings> calls;

private:
    std::vector<Strings> replies_;
};

TEST(TranslateAlignedTest, SendsOneBatchWhenTheReplyLinesUp) {
    FakeBatch fake({Strings{"一", "二"}});
    const Strings sources{"one", "two"};
    EXPECT_EQ(translateAligned(sources, std::ref(fake)), (Strings{"一", "二"}));
    EXPECT_EQ(fake.calls.size(), 1u);
}

TEST(TranslateAlignedTest, RetriesTheWholeBatchOnce) {
    // 第一次少一段，重試就正常了（LLM 有隨機性）
    FakeBatch fake({Strings{"一"}, Strings{"一", "二"}});
    const Strings sources{"one", "two"};
    EXPECT_EQ(translateAligned(sources, std::ref(fake)), (Strings{"一", "二"}));
    EXPECT_EQ(fake.calls.size(), 2u);
    EXPECT_EQ(fake.calls[1].size(), 2u) << "重試仍然是整批";
}

TEST(TranslateAlignedTest, FallsBackToOneSegmentAtATime) {
    // 兩次整批都多一段，改成逐段重送
    FakeBatch fake(
        {Strings{"一", "二", "多"}, Strings{"一", "二", "多"}, Strings{"一"}, Strings{"二"}});
    const Strings sources{"one", "two"};
    EXPECT_EQ(translateAligned(sources, std::ref(fake)), (Strings{"一", "二"}));
    ASSERT_EQ(fake.calls.size(), 4u);
    EXPECT_EQ(fake.calls[2], (Strings{"one"}));
    EXPECT_EQ(fake.calls[3], (Strings{"two"}));
}

TEST(TranslateAlignedTest, FallsBackWhenTheReplyIsUnparsable) {
    // 引擎解析不出 JSON 時丟 BadResponse，一樣要退到逐段
    FakeBatch fake({Strings{}, Strings{}, Strings{"一"}, Strings{"二"}});
    const Strings sources{"one", "two"};
    EXPECT_EQ(translateAligned(sources, std::ref(fake)), (Strings{"一", "二"}));
    EXPECT_EQ(fake.calls.size(), 4u);
}

TEST(TranslateAlignedTest, DoesNotRetryNetworkOrQuotaErrors) {
    // 額度用完時再送只是多花錢和時間，直接往外丟讓引擎鏈換下一個
    int calls = 0;
    const BatchTranslate quotaExceeded = [&calls](std::span<const std::string>) -> Strings {
        ++calls;
        throw TranslatorError(TranslateError::RateLimited, "429");
    };
    const Strings sources{"one", "two"};
    EXPECT_THROW(translateAligned(sources, quotaExceeded), TranslatorError);
    EXPECT_EQ(calls, 1);
}

TEST(TranslateAlignedTest, DoesNotRetryAfterCancel) {
    int calls = 0;
    const BatchTranslate cancelled = [&calls](std::span<const std::string>) -> Strings {
        ++calls;
        throw TranslatorError(TranslateError::Cancelled, "取消");
    };
    EXPECT_THROW(translateAligned(Strings{"one", "two"}, cancelled), TranslatorError);
    EXPECT_EQ(calls, 1);
}

TEST(TranslateAlignedTest, GivesUpWhenASingleSegmentKeepsFailing) {
    // 只有一段時，逐段重送不會有任何改變，直接失敗讓引擎鏈換下一個
    FakeBatch fake({Strings{"甲", "乙"}, Strings{"甲", "乙"}});
    EXPECT_THROW(translateAligned(Strings{"one"}, std::ref(fake)), TranslatorError);
    EXPECT_EQ(fake.calls.size(), 2u);
}

TEST(TranslateAlignedTest, DoesNotCallTheEngineForAnEmptyBatch) {
    int calls = 0;
    const BatchTranslate counting = [&calls](std::span<const std::string>) -> Strings {
        ++calls;
        return {};
    };
    EXPECT_TRUE(translateAligned({}, counting).empty());
    EXPECT_EQ(calls, 0);
}

}  // namespace
}  // namespace tmw::core
