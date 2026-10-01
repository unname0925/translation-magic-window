#include "core/glossary.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace tmw::core {
namespace {

TEST(GlossaryTest, ReadsOneEntryPerLine) {
    const GlossaryLoad load = parseGlossary("悠真=悠真\nエクスカリバー=王者之劍\n");
    EXPECT_TRUE(load.problems.empty());
    EXPECT_EQ(load.entries, (Glossary{{"悠真", "悠真"}, {"エクスカリバー", "王者之劍"}}));
}

TEST(GlossaryTest, SkipsCommentsAndBlankLinesAndTrimsSpaces) {
    // 記事本存檔時的 BOM、Windows 的換行、中文輸入法的全形空白和全形等號
    const GlossaryLoad load = parseGlossary(
        "\xEF\xBB\xBF# 註解\r\n\r\n  悠真 = 悠真 \r\n\xE3\x80\x80鬼\xEF\xBC\x9D惡鬼\r\n");
    EXPECT_TRUE(load.problems.empty());
    EXPECT_EQ(load.entries, (Glossary{{"悠真", "悠真"}, {"鬼", "惡鬼"}}));
}

TEST(GlossaryTest, ReportsLinesItCannotReadAndKeepsTheRest) {
    const GlossaryLoad load = parseGlossary("悠真\n=沒有原文\n鬼=\n魔王=魔王\n");
    EXPECT_EQ(load.entries, (Glossary{{"魔王", "魔王"}}));
    ASSERT_EQ(load.problems.size(), 3u);
    EXPECT_EQ(load.problems[0], "第 1 行沒有「=」");
}

TEST(GlossaryTest, OnlyTheFirstEqualsSignSeparates) {
    EXPECT_EQ(parseGlossary("A=B=C").entries, (Glossary{{"A", "B=C"}}));
}

TEST(GlossaryTest, TheLaterEntryWins) {
    EXPECT_EQ(parseGlossary("鬼=鬼\n鬼=惡鬼").entries, (Glossary{{"鬼", "惡鬼"}}));
}

TEST(GlossaryTest, SendsOnlyTheWordsOnScreen) {
    const Glossary all{{"悠真", "悠真"}, {"魔王", "魔王"}, {"Excalibur", "王者之劍"}};
    const std::vector<std::string> segments{"悠真、逃げろ！", "何だと？"};
    EXPECT_EQ(glossaryFor(segments, all), (Glossary{{"悠真", "悠真"}}));
}

TEST(GlossaryTest, FingerprintCoversOnlyWordsInThatText) {
    const Glossary all{{"悠真", "悠真"}, {"魔王", "魔王"}};
    EXPECT_EQ(glossaryFingerprint("悠真、逃げろ！", all), "悠真=悠真\n");
    EXPECT_EQ(glossaryFingerprint("何だと？", all), "") << "沒有用到詞表的文字，快取鍵不變";
}

TEST(GlossaryTest, TheTemplateHasNoEntries) {
    // 範本全是註解：第一次建立檔案時不能冒出假的詞條
    const GlossaryLoad load = parseGlossary(glossaryTemplate());
    EXPECT_TRUE(load.entries.empty());
    EXPECT_TRUE(load.problems.empty());
}

}  // namespace
}  // namespace tmw::core
