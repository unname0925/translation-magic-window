// ルビ的一般讀音／特殊讀音（M2-13）。用手寫的小讀音表；真的讀音表另外由整合測試對照。
#include "core/furigana_readings.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tmw::core {
namespace {

// 和 tools/eval/furigana_dict.py build 產生的格式相同
constexpr std::string_view kTable =
    "# 註解\n"
    "K 本\tほん,もと\n"
    "K 気\tき,け\n"
    "K 東\tとう,ひがし\n"
    "K 京\tきょう,けい\n"
    "K 学\tがく,まな\n"
    "K 校\tこう\n"
    "K 人\tじん,にん,ひと\n"
    "K 戦\tせん,いくさ,たたか,たたかう\n"
    "K 今\tこん,いま\n"
    "K 日\tにち,ひ,か\n"
    "K 母\tぼ,はは\n"
    "K 強\tきょう,つよ\n"
    "K 敵\tてき,かたき\n"
    "W 今日\tきょう\n"
    "W 母\tかあ\n";

FuriganaReadings table() {
    return FuriganaReadings::parse(kTable).value();
}

TEST(FuriganaReadingsTest, OrdinaryReadingsAreSpelledOutKanjiByKanji) {
    EXPECT_EQ(table().isSpecial("東京", "とうきょう"), false);
    EXPECT_EQ(table().isSpecial("本気", "ほんき"), false);
    EXPECT_EQ(table().isSpecial("戦", "たたか"), false) << "送假名在ルビ外面";
}

TEST(FuriganaReadingsTest, SoundChangesAreAllowed) {
    EXPECT_EQ(table().isSpecial("学校", "がっこう"), false) << "促音：がく→がっ";
    EXPECT_EQ(table().isSpecial("人々", "ひとびと"), false) << "々重複前一個字，加上連濁";
}

TEST(FuriganaReadingsTest, IrregularButOrdinaryReadingsComeFromTheDictionary) {
    EXPECT_EQ(table().isSpecial("今日", "きょう"), false) << "熟字訓";
    EXPECT_EQ(table().isSpecial("母", "かあ"), false) << "お母さん只標在母上";
}

TEST(FuriganaReadingsTest, TheAuthorsOwnReadingsAreSpecial) {
    EXPECT_EQ(table().isSpecial("本気", "マジ"), true);
    EXPECT_EQ(table().isSpecial("強敵", "とも"), true) << "平假名的特殊讀音，片假名規則抓不到";
}

TEST(FuriganaReadingsTest, KatakanaReadingsAreComparedAsHiragana) {
    EXPECT_EQ(table().isSpecial("東京", "トウキョウ"), false);
}

TEST(FuriganaReadingsTest, UnknownKanjiCannotBeJudged) {
    EXPECT_EQ(table().isSpecial("楓林", "ふうりん"), std::nullopt);
}

TEST(FuriganaReadingsTest, AnEmptyTableIsRejected) {
    EXPECT_FALSE(FuriganaReadings::parse("# 只有註解\n").has_value());
}

TEST(FuriganaReadingsTest, KatakanaBecomesHiragana) {
    EXPECT_EQ(toHiragana("マジ・ロンドン"), "まじ・ろんどん");
}

// 用真的讀音表（tools/eval/furigana_dict.py build 產生在 models/furigana/）對照 Python 版的結果：
// C++ 和 Python 的判斷要一模一樣。讀音表不在（CI、還沒產生）時略過。
TEST(FuriganaReadingsTest, TheRealTableAgreesWithThePythonEvaluation) {
    const std::filesystem::path path =
        std::filesystem::path(TMW_MODELS_DIR) / "furigana" / "readings.tsv";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "沒有 " << path.string();
    }
    std::ifstream file(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    const std::optional<FuriganaReadings> real = FuriganaReadings::parse(text);
    ASSERT_TRUE(real.has_value());

    // tools/eval/evaluate_furigana.py 的例子，期望值是 Python 版實際的判斷
    for (const auto& [base, reading] :
         std::vector<std::pair<std::string, std::string>>{{"本気", "マジ"},
                                                          {"強敵", "とも"},
                                                          {"地球", "ほし"},
                                                          {"宇宙", "そら"},
                                                          {"冗談", "ジョーク"},
                                                          {"好敵手", "ライバル"},
                                                          {"楓男", "フーダン"},
                                                          {"環境", "フローラ"}}) {
        EXPECT_EQ(real->isSpecial(base, reading), true) << base << "=" << reading;
    }
    for (const auto& [base, reading] :
         std::vector<std::pair<std::string, std::string>>{{"今日", "きょう"},
                                                          {"学校", "がっこう"},
                                                          {"人々", "ひとびと"},
                                                          {"戦", "たたか"},
                                                          {"倫敦", "ロンドン"},
                                                          {"珈琲", "コーヒー"},
                                                          {"手紙", "てがみ"},
                                                          {"鉄砲", "てっぽう"},
                                                          {"楓林", "ふうりん"},
                                                          {"母", "かあ"},
                                                          {"二十歳", "はたち"},
                                                          {"本気", "ほんき"}}) {
        EXPECT_EQ(real->isSpecial(base, reading), false) << base << "=" << reading;
    }
}

}  // namespace
}  // namespace tmw::core
