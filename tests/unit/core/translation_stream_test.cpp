// 邊收邊解析：串流時收完一段就要能先顯示一段（design.md 4.6）。
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/translation_alignment.h"

namespace tmw::core {
namespace {

using Strings = std::vector<std::string>;

TEST(ParseJsonArrayPrefixTest, ReturnsOnlyTheSegmentsThatAreComplete) {
    EXPECT_EQ(parseJsonArrayPrefix(R"([")"), (Strings{}));
    EXPECT_EQ(parseJsonArrayPrefix(R"(["你)"), (Strings{})) << "還在傳這一段";
    EXPECT_EQ(parseJsonArrayPrefix(R"(["你好")"), (Strings{"你好"}));
    EXPECT_EQ(parseJsonArrayPrefix(R"(["你好", )"), (Strings{"你好"}));
    EXPECT_EQ(parseJsonArrayPrefix(R"(["你好", "再)"), (Strings{"你好"}));
    EXPECT_EQ(parseJsonArrayPrefix(R"(["你好", "再見"])"), (Strings{"你好", "再見"}));
}

TEST(ParseJsonArrayPrefixTest, HandlesNothingUsefulYet) {
    EXPECT_EQ(parseJsonArrayPrefix(""), (Strings{}));
    EXPECT_EQ(parseJsonArrayPrefix("["), (Strings{}));
    EXPECT_EQ(parseJsonArrayPrefix("```json\n"), (Strings{}));
    EXPECT_EQ(parseJsonArrayPrefix("[]"), (Strings{}));
}

TEST(ParseJsonArrayPrefixTest, DoesNotStopAtBracketsInsideText) {
    EXPECT_EQ(parseJsonArrayPrefix(R"(["[系統] 好"，)"), (Strings{"[系統] 好"}));
    EXPECT_EQ(parseJsonArrayPrefix(R"(["結束]")"), (Strings{"結束]"}));
    EXPECT_EQ(parseJsonArrayPrefix(R"(["他說\"走吧\"")"), (Strings{"他說\"走吧\""}));
}

TEST(ParseJsonArrayPrefixTest, WorksInsideACodeFence) {
    EXPECT_EQ(parseJsonArrayPrefix("```json\n[\"你好\", \"再"), (Strings{"你好"}));
}

TEST(ParseJsonArrayPrefixTest, FindsTheArrayInsideAnObject) {
    // 本機的 hy-mt2 真的會把陣列包在物件裡回傳（tests/data/net/openai_ja_batch.json）
    EXPECT_EQ(parseJsonArrayPrefix(R"({"source_lang": "ja", "segments": ["你好", "再見"]})"),
              (Strings{"你好", "再見"}));
}

TEST(ParseJsonArrayPrefixTest, GrowsOneSegmentAtATime) {
    // 模擬真的一個字一個字收進來
    const std::string full = R"(["你好", "再見", "存檔"])";
    Strings last;
    for (std::size_t i = 0; i <= full.size(); ++i) {
        const Strings ready = parseJsonArrayPrefix(std::string_view(full).substr(0, i));
        EXPECT_GE(ready.size(), last.size()) << "已經顯示的段落不可以又變少";
        for (std::size_t j = 0; j < last.size(); ++j) {
            EXPECT_EQ(ready[j], last[j]) << "已經顯示的段落不可以改內容";
        }
        last = ready;
    }
    EXPECT_EQ(last, (Strings{"你好", "再見", "存檔"}));
}

}  // namespace
}  // namespace tmw::core
