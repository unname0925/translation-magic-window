// 振り仮名（ルビ）的辨識與附著（design.md 4.4）。
// 判斷依據是幾何關係：字比本文小很多、緊貼著本文那一欄、只蓋住其中一部分、而且全是假名。
#include "core/ruby.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace tmw::core {
namespace {

// 直排的一欄。x 是欄的左右，y 是這一欄從哪裡到哪裡。
OcrLine column(int left, int right, int top, int bottom, std::string text) {
    return OcrLine{
        RectI{left, top, right, bottom}, std::move(text), 0.9f, Orientation::Vertical, {}};
}

OcrLine row(int left, int right, int top, int bottom, std::string text) {
    return OcrLine{
        RectI{left, top, right, bottom}, std::move(text), 0.9f, Orientation::Horizontal, {}};
}

TEST(IsKanaOnlyTest, OnlyKanaCountsAsRuby) {
    EXPECT_TRUE(isKanaOnly("マジ"));
    EXPECT_TRUE(isKanaOnly("しんれんさい"));
    EXPECT_TRUE(isKanaOnly("ドキドキ"));
    EXPECT_FALSE(isKanaOnly("本気")) << "漢字不是ルビ";
    EXPECT_FALSE(isKanaOnly("マジy")) << "夾了英文字母";
    EXPECT_FALSE(isKanaOnly("110")) << "沒有假名";
    EXPECT_FALSE(isKanaOnly(""));
}

TEST(AttachRubyTest, AttachesASmallKanaColumnToTheKanjiItAnnotates) {
    // 本文 6 個字佔 y=50～250，ルビ 蓋住最上面 1/3 → 前 2 個字
    const std::vector<OcrLine> lines{column(100, 140, 50, 250, "本気で戦うぞ"),
                                     column(138, 156, 50, 116, "マジ")};
    const RubyResult result = attachRuby(lines);
    ASSERT_EQ(result.lines.size(), 1u) << "ルビ 那一行不再是獨立的一行";
    EXPECT_EQ(result.attached, 1);
    ASSERT_EQ(result.lines[0].ruby.size(), 1u);
    EXPECT_EQ(result.lines[0].ruby[0].start, 0);
    EXPECT_EQ(result.lines[0].ruby[0].length, 2);
    EXPECT_EQ(result.lines[0].ruby[0].reading, "マジ");
}

TEST(AttachRubyTest, WorksWhenTheBoxesOverlap) {
    // 實測日文漫畫時，ルビ 的框和本文重疊（間距中位數 -0.17，design.md 4.4）
    const std::vector<OcrLine> lines{column(100, 140, 50, 250, "本気で戦うぞ"),
                                     column(130, 148, 50, 116, "マジ")};
    const RubyResult result = attachRuby(lines);
    EXPECT_EQ(result.attached, 1);
}

TEST(AttachRubyTest, FindsTheRangeInTheMiddleOfAColumn) {
    // 6 個字佔 y=50～250，ルビ 在 y=150～183 → 第 3 個字
    const std::vector<OcrLine> lines{column(100, 140, 50, 250, "あい本気うぞ"),
                                     column(138, 156, 150, 183, "マジ")};
    const RubyResult result = attachRuby(lines);
    ASSERT_EQ(result.lines.size(), 1u);
    ASSERT_EQ(result.lines[0].ruby.size(), 1u);
    EXPECT_EQ(result.lines[0].ruby[0].start, 3);
    EXPECT_GE(result.lines[0].ruby[0].length, 1);
}

TEST(AttachRubyTest, HorizontalRubySitsAboveTheLine) {
    const std::vector<OcrLine> lines{row(100, 300, 100, 140, "本気で戦うぞ"),
                                     row(100, 166, 82, 100, "マジ")};
    const RubyResult result = attachRuby(lines);
    ASSERT_EQ(result.lines.size(), 1u);
    ASSERT_EQ(result.lines[0].ruby.size(), 1u);
    EXPECT_EQ(result.lines[0].ruby[0].start, 0);
}

TEST(AttachRubyTest, TwoAnnotationsOnOneColumnComeOutInOrder) {
    const std::vector<OcrLine> lines{column(100, 140, 50, 250, "本気で戦うぞ"),
                                     column(138, 156, 150, 216, "たたか"),
                                     column(138, 156, 50, 116, "マジ")};
    const RubyResult result = attachRuby(lines);
    ASSERT_EQ(result.lines.size(), 1u);
    ASSERT_EQ(result.lines[0].ruby.size(), 2u);
    EXPECT_EQ(result.lines[0].ruby[0].reading, "マジ") << "依本文中的位置排序";
    EXPECT_EQ(result.lines[0].ruby[1].reading, "たたか");
}

TEST(AttachRubyTest, LeavesOrdinaryLinesAlone) {
    // 一樣大的鄰欄是另一句話，不是ルビ
    const std::vector<OcrLine> lines{column(100, 140, 50, 250, "本気で戦うぞ"),
                                     column(150, 190, 50, 250, "よろしくたのむ")};
    const RubyResult result = attachRuby(lines);
    EXPECT_EQ(result.lines.size(), 2u);
    EXPECT_EQ(result.attached, 0);
}

TEST(AttachRubyTest, SmallKanjiIsNotRuby) {
    const std::vector<OcrLine> lines{column(100, 140, 50, 250, "本気で戦うぞ"),
                                     column(138, 156, 50, 116, "注")};
    EXPECT_EQ(attachRuby(lines).attached, 0) << "ルビ 一定是假名";
}

TEST(AttachRubyTest, SomethingCoveringTheWholeColumnIsNotRuby) {
    const std::vector<OcrLine> lines{column(100, 140, 50, 250, "本気で戦うぞ"),
                                     column(138, 156, 50, 250, "ちいさいもじのせりふ")};
    EXPECT_EQ(attachRuby(lines).attached, 0) << "蓋住整欄的是另一句話";
}

TEST(AttachRubyTest, SomethingFarAwayIsNotRuby) {
    const std::vector<OcrLine> lines{column(100, 140, 50, 250, "本気で戦うぞ"),
                                     column(400, 418, 50, 116, "マジ")};
    EXPECT_EQ(attachRuby(lines).attached, 0);
}

TEST(AttachRubyTest, PicksTheNearestColumn) {
    const std::vector<OcrLine> lines{column(100, 140, 50, 250, "本気で戦うぞ"),
                                     column(200, 240, 50, 250, "よろしくたのむ"),
                                     column(196, 214, 50, 116, "マジ")};
    const RubyResult result = attachRuby(lines);
    ASSERT_EQ(result.lines.size(), 2u);
    EXPECT_EQ(result.attached, 1);
    // 附到比較近的那一欄（第二欄），不是第一欄
    EXPECT_TRUE(result.lines[0].ruby.empty());
    EXPECT_EQ(result.lines[1].ruby.size(), 1u);
}

TEST(MarkRubyTest, WrapsTheAnnotatedCharacters) {
    const std::vector<RubyAnnotation> ruby{{0, 2, "マジ"}};
    EXPECT_EQ(markRuby("本気で戦うぞ", ruby), "{本気|マジ}で戦うぞ");
}

TEST(MarkRubyTest, HandlesSeveralAnnotations) {
    const std::vector<RubyAnnotation> ruby{{0, 2, "マジ"}, {3, 1, "たたか"}};
    EXPECT_EQ(markRuby("本気で戦うぞ", ruby), "{本気|マジ}で{戦|たたか}うぞ");
}

TEST(MarkRubyTest, AnnotationAtTheEnd) {
    const std::vector<RubyAnnotation> ruby{{3, 2, "みらい"}};
    EXPECT_EQ(markRuby("これは未来", ruby), "これは{未来|みらい}");
}

TEST(MarkRubyTest, ALengthPastTheEndStopsAtTheEnd) {
    const std::vector<RubyAnnotation> ruby{{3, 9, "みらい"}};
    EXPECT_EQ(markRuby("これは未来", ruby), "これは{未来|みらい}");
}

TEST(MarkRubyTest, SkipsOverlappingAnnotations) {
    // 位置算錯時不要產生壞掉的標記（翻譯引擎的對齊檢查會被它搞混）
    const std::vector<RubyAnnotation> ruby{{0, 3, "まえ"}, {1, 2, "あと"}};
    EXPECT_EQ(markRuby("本気で戦うぞ", ruby), "{本気で|まえ}戦うぞ");
}

TEST(MarkRubyTest, NoAnnotationsMeansNoChange) {
    EXPECT_EQ(markRuby("本気で戦うぞ", {}), "本気で戦うぞ");
}

TEST(MarkRubyTest, IgnoresAnnotationsPastTheEnd) {
    const std::vector<RubyAnnotation> ruby{{99, 2, "マジ"}};
    EXPECT_EQ(markRuby("本気", ruby), "本気");
}

}  // namespace
}  // namespace tmw::core
