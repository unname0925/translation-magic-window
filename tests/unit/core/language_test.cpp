// UT-04：語言判斷（假名、韓文字母、純漢字、英文、混合文字）。
#include "core/language.h"

#include <gtest/gtest.h>

namespace tmw::core {
namespace {

TEST(LanguageTest, KanaIsJapanese) {
    EXPECT_EQ(detectLanguage("えーマジで?てかさー"), Language::Japanese);
    EXPECT_EQ(detectLanguage("ドキドキ"), Language::Japanese);
    EXPECT_EQ(detectLanguage("ｾｰﾌﾞ"), Language::Japanese) << "半形片假名";
}

TEST(LanguageTest, HangulIsKorean) {
    EXPECT_EQ(detectLanguage("이 대통령"), Language::Korean);
    EXPECT_EQ(detectLanguage("ㄱㄴㄷ"), Language::Korean) << "相容字母";
}

TEST(LanguageTest, HanOnlyCountsAsJapanese) {
    // 來源語言不包含中文，所以純漢字一律當作日文（design.md 4.4）
    EXPECT_EQ(detectLanguage("証拠品人物"), Language::Japanese);
}

TEST(LanguageTest, LatinIsEnglish) {
    EXPECT_EQ(detectLanguage("THAT'S A PERK?"), Language::English);
    EXPECT_EQ(detectLanguage("Ｅｎｇｌｉｓｈ"), Language::English) << "全形英文";
}

TEST(LanguageTest, MixedTextPrefersTheScriptThatOnlyOneLanguageUses) {
    // 日文常夾英文和漢字
    EXPECT_EQ(detectLanguage("NHKのお手続き"), Language::Japanese);
    // 韓文新聞標題常夾漢字和英文
    EXPECT_EQ(detectLanguage("[LIVE] 뉴스9: 이대통령"), Language::Korean);
    EXPECT_EQ(detectLanguage("美 중국 반도체"), Language::Korean);
}

TEST(LanguageTest, TextWithoutAnyLetterIsUnknown) {
    EXPECT_EQ(detectLanguage(""), Language::Unknown);
    EXPECT_EQ(detectLanguage("110/110"), Language::Unknown);
    EXPECT_EQ(detectLanguage("00:35 ... !!"), Language::Unknown);
}

TEST(LanguageTest, CountsEachScript) {
    const ScriptCounts counts = countScripts("あ漢A가1");
    EXPECT_EQ(counts, (ScriptCounts{.kana = 1, .han = 1, .hangul = 1, .latin = 1, .other = 1}));
}

TEST(LanguageTest, BrokenUtf8DoesNotCrash) {
    // OCR 的輸出理論上是合法的 UTF-8，但純函式不該因為輸入不合法就當掉
    EXPECT_NO_THROW(detectLanguage("\xE3\x81"));  // 截斷的三位元組字元
    EXPECT_NO_THROW(detectLanguage("\xFF\xFE"));  // 不合法的開頭位元組
    EXPECT_EQ(detectLanguage("\xE3\x81 abc"), Language::English);
}

TEST(LanguageTest, CodesMatchTheEnginesAndGroundTruth) {
    EXPECT_EQ(languageCode(Language::Japanese), "ja");
    EXPECT_EQ(languageCode(Language::English), "en");
    EXPECT_EQ(languageCode(Language::Korean), "ko");
    EXPECT_EQ(languageCode(Language::Unknown), "");
}

// 兩個辨識模型都跑過之後，整張一起決定用哪一邊（M2-04、design.md 4.4）
TEST(ChooseScriptTest, KoreanPagesPickTheKoreanModel) {
    // 實測：日文模型讀韓文漫畫只讀得出空字串和網址浮水印，韓文模型讀得出內容
    EXPECT_EQ(chooseScript("  novelagit.xyz", "요건 어때? 아지문소설 novelagit.xyz"),
              Language::Korean);
}

TEST(ChooseScriptTest, JapanesePagesKeepTheMainModel) {
    // 韓文模型讀日文會亂讀，可能吐出幾個韓文字母，但假名和漢字的數量遠遠更多
    EXPECT_EQ(chooseScript("今日はいい天気ですね", "오늘"), Language::Japanese);
}

TEST(ChooseScriptTest, EnglishPagesKeepTheMainModel) {
    EXPECT_EQ(chooseScript("The quick brown fox", "The quick brown fox"), Language::English);
}

TEST(ChooseScriptTest, NoHangulMeansTheMainModel) {
    EXPECT_EQ(chooseScript("こんにちは", ""), Language::Japanese);
}

TEST(ChooseScriptTest, NothingReadableIsUnknown) {
    // 只有數字和符號：分不出來，呼叫端會沿用上一次的決定
    EXPECT_EQ(chooseScript("123 -- 456", "123 -- 456"), Language::Unknown);
    EXPECT_EQ(chooseScript("", ""), Language::Unknown);
}

TEST(ChooseScriptTest, ATieGoesToKorean) {
    // 韓文畫面上主模型常常讀出幾個假名的雜訊。一樣多的時候相信韓文那一邊，
    // 因為主模型讀韓文的錯誤率（94%）遠高於韓文模型讀日文時我們會損失的部分。
    EXPECT_EQ(chooseScript("あい", "가나"), Language::Korean);
}

TEST(ChooseScriptTest, MoreKanaThanHangulKeepsTheMainModel) {
    EXPECT_EQ(chooseScript("あいうえお", "가"), Language::Japanese);
}

}  // namespace
}  // namespace tmw::core
