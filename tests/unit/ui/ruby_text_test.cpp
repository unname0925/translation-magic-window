// `{本文|讀音}` 的解析。排版和繪製需要字型，在整合測試裡。
#include "ui/ruby_text.h"

#include <gtest/gtest.h>

#include <vector>

namespace tmw::ui {
namespace {

using Runs = std::vector<RubyRun>;

RubyRun plain(const char* text) {
    return RubyRun{QString::fromUtf8(text), {}};
}

RubyRun ruby(const char* base, const char* reading) {
    return RubyRun{QString::fromUtf8(base), QString::fromUtf8(reading)};
}

TEST(ParseRubyMarkupTest, PlainTextIsOneRun) {
    EXPECT_EQ(parseRubyMarkup(QStringLiteral("我要認真打一場")), (Runs{plain("我要認真打一場")}));
    EXPECT_TRUE(parseRubyMarkup(QString()).empty());
}

TEST(ParseRubyMarkupTest, SplitsOutTheAnnotatedPart) {
    EXPECT_EQ(parseRubyMarkup(QStringLiteral("我要{認真|玩真的}打一場")),
              (Runs{plain("我要"), ruby("認真", "玩真的"), plain("打一場")}));
}

TEST(ParseRubyMarkupTest, HandlesSeveralAndTheEnds) {
    EXPECT_EQ(parseRubyMarkup(QStringLiteral("{認真|玩真的}和{未來|明天}")),
              (Runs{ruby("認真", "玩真的"), plain("和"), ruby("未來", "明天")}));
}

TEST(ParseRubyMarkupTest, UnmatchedBracesStayAsText) {
    // 譯文本身可能含有大括號，不能把後面的內容吃掉
    EXPECT_EQ(parseRubyMarkup(QStringLiteral("這是{一個測試")), (Runs{plain("這是{一個測試")}));
    EXPECT_EQ(parseRubyMarkup(QStringLiteral("這是}一個測試")), (Runs{plain("這是}一個測試")}));
    EXPECT_EQ(parseRubyMarkup(QStringLiteral("沒有直線{的括號}")),
              (Runs{plain("沒有直線{的括號}")}));
}

TEST(ParseRubyMarkupTest, KeepsTextAfterABrokenMarker) {
    const Runs runs = parseRubyMarkup(QStringLiteral("{壞掉的 然後{好的|讀音}"));
    ASSERT_FALSE(runs.empty());
    EXPECT_EQ(runs.back(), ruby("好的", "讀音"));
}

}  // namespace
}  // namespace tmw::ui
