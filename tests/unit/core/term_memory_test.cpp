// 名詞記憶：找名字、記住譯名（先記的為準）、存檔、英文不分大小寫。
#include "core/term_memory.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace tmw::core {
namespace {

using Terms = std::vector<std::string>;

TEST(FindTermsTest, JapaneseNamesAndKatakana) {
    EXPECT_EQ(findTerms("山田さんはどこ？"), (Terms{"山田"}));
    EXPECT_EQ(findTerms("鈴木先輩と佐藤君"), (Terms{"鈴木", "佐藤"}));
    EXPECT_EQ(findTerms("マリアンヌが来た"), (Terms{"マリアンヌ"}));
    EXPECT_TRUE(findTerms("先生、ドン").empty()) << "敬稱本身、兩個字的片假名不算";
}

TEST(FindTermsTest, RomanizedNamesInEnglish) {
    EXPECT_EQ(findTerms("TANAKA-KUN, WAIT!"), (Terms{"TANAKA"}));
    EXPECT_EQ(findTerms("Where is Haruka? Haruka's bag is here."), (Terms{"Haruka"}))
        << "同一個名字只算一次；所有格去掉；Where、here 是常用字";
    EXPECT_TRUE(findTerms("I THINK SO TOO").empty());
    EXPECT_TRUE(findTerms("STRAWBERRY CAKE").empty()) << "不像日文拼音的字不是名字";
}

TEST(FindTermsTest, HandlesAndAddressesAreNotNames) {
    EXPECT_TRUE(findTerms("@azu_knzm").empty()) << "帳號";
    EXPECT_TRUE(findTerms("discord.gg/hanako").empty()) << "網址";
    EXPECT_EQ(findTerms("HANAKO, @tanaka_99"), (Terms{"HANAKO"}));
}

TEST(TermMemoryTest, BadTranslationsAreNotRemembered) {
    EXPECT_FALSE(isUsableTermTranslation("讀音：ミツキ")) << "照抄提示詞的用語";
    EXPECT_FALSE(isUsableTermTranslation("本文")) << "照抄提示詞的用語";
    EXPECT_FALSE(isUsableTermTranslation("ミツキ")) << "沒翻成中文";
    EXPECT_FALSE(isUsableTermTranslation("我覺得你應該先回家休息一下比較好")) << "一整句";
    EXPECT_TRUE(isUsableTermTranslation("美月"));
    EXPECT_TRUE(isUsableTermTranslation("TANAKA")) << "留英文的不算錯（只是沒翻）";
    TermMemory memory;
    memory.remember("site", {{"Mitsuki", "讀音：ミツキ"}, {"Haruka", "春香"}});
    EXPECT_EQ(memory.size("site"), 1u);
}

TEST(TermMemoryTest, TheFirstTranslationWins) {
    TermMemory memory;
    EXPECT_EQ(memory.missing("site", Terms{"山田", "鈴木"}), (Terms{"山田", "鈴木"}));
    memory.remember("site", {{"山田", "山田"}});
    memory.remember("site", {{"山田", "山本"}, {"鈴木", "鈴木"}});
    EXPECT_EQ(memory.lookup("site", Terms{"山田", "鈴木"}),
              (Glossary{{"山田", "山田"}, {"鈴木", "鈴木"}}))
        << "好幾頁同時翻譯時，先記下的為準";
    EXPECT_TRUE(memory.missing("site", Terms{"山田"}).empty());
    EXPECT_TRUE(memory.lookup("other site", Terms{"山田"}).empty()) << "依範圍分開";
}

TEST(TermMemoryTest, EnglishKeysIgnoreCase) {
    TermMemory memory;
    memory.remember("site", {{"Tanaka", "田中"}});
    EXPECT_EQ(memory.lookup("site", Terms{"TANAKA"}), (Glossary{{"TANAKA", "田中"}}))
        << "照這一頁的寫法回傳，專有名詞表才對得上原文";
    EXPECT_TRUE(memory.missing("site", Terms{"tanaka"}).empty());
}

TEST(TermMemoryTest, EmptyTranslationsAreNotRemembered) {
    TermMemory memory;
    memory.remember("site", {{"山田", ""}});
    EXPECT_EQ(memory.size("site"), 0u);
}

TEST(TermMemoryTest, SurvivesARestartAndDropsTheOldestWhenFull) {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "tmw-term-memory-test" / "terms.json";
    std::filesystem::remove_all(file.parent_path());
    {
        TermMemory memory(file, 2);
        memory.remember("site", {{"山田", "山田"}});
        memory.remember("site", {{"鈴木", "鈴木"}});
        memory.remember("site", {{"佐藤", "佐藤"}});  // 超過 2 個：丟掉最早記的
    }
    TermMemory reopened(file, 2);
    EXPECT_EQ(reopened.size("site"), 2u);
    EXPECT_TRUE(reopened.lookup("site", Terms{"山田"}).empty());
    EXPECT_EQ(reopened.lookup("site", Terms{"佐藤"}), (Glossary{{"佐藤", "佐藤"}}));
    std::filesystem::remove_all(file.parent_path());
}

}  // namespace
}  // namespace tmw::core
