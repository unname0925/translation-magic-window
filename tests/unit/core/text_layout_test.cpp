// UT-03：合併段落與閱讀順序（英文斷字接回、日文直排由右到左、被邊緣切到的區塊會被過濾）。
#include "core/text_layout.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace tmw::core {
namespace {

OcrLine horizontal(int x, int y, int width, int height, std::string text, float score = 0.9f) {
    return {RectI::fromXYWH(x, y, width, height), std::move(text), score, Orientation::Horizontal};
}

OcrLine vertical(int x, int y, int width, int height, std::string text, float score = 0.9f) {
    return {RectI::fromXYWH(x, y, width, height), std::move(text), score, Orientation::Vertical};
}

std::vector<std::string> texts(const std::vector<TextBlock>& blocks) {
    std::vector<std::string> result;
    for (const TextBlock& block : blocks) {
        result.push_back(block.text);
    }
    return result;
}

TEST(ReadingOrderTest, HorizontalGoesTopToBottomThenLeftToRight) {
    std::vector<OcrLine> lines = {horizontal(200, 100, 80, 20, "B"),
                                  horizontal(10, 100, 80, 20, "A"),
                                  horizontal(10, 140, 80, 20, "C")};

    sortReadingOrder(lines);

    EXPECT_EQ(lines[0].text, "A");
    EXPECT_EQ(lines[1].text, "B");
    EXPECT_EQ(lines[2].text, "C");
}

TEST(ReadingOrderTest, SlightlyMisalignedLinesStillCountAsTheSameRow) {
    // OCR 的框上下差幾個像素很常見，不該因此把同一行拆成兩行
    std::vector<OcrLine> lines = {horizontal(200, 102, 80, 20, "B"),
                                  horizontal(10, 100, 80, 20, "A")};

    sortReadingOrder(lines);

    EXPECT_EQ(lines[0].text, "A");
}

TEST(ReadingOrderTest, VerticalGoesRightToLeft) {
    // 日文漫畫：直排的欄位由右到左，同一欄由上到下。
    // 直排的字級是「欄長 ÷ 字數」，所以測試資料的長度要和字數相符，
    // 否則就等於在說「這兩欄的字大小不同」，本來就不該合併。
    std::vector<OcrLine> lines = {vertical(100, 10, 20, 20, "左"),
                                  vertical(200, 60, 20, 40, "右下"),
                                  vertical(200, 10, 20, 40, "右上")};

    sortReadingOrder(lines);

    EXPECT_EQ(texts(mergeIntoBlocks(lines)).size(), 2u);
    EXPECT_EQ(lines[0].text, "右上");
    EXPECT_EQ(lines[1].text, "右下");
    EXPECT_EQ(lines[2].text, "左");
}

TEST(MergeTest, StackedLinesBecomeOneBlock) {
    const std::vector<OcrLine> lines = {horizontal(10, 100, 200, 20, "the first line"),
                                        horizontal(10, 124, 180, 20, "and the second")};

    const std::vector<TextBlock> blocks = mergeIntoBlocks(lines);

    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].text, "the first line and the second");
    EXPECT_EQ(blocks[0].rect, (RectI{10, 100, 210, 144}));
    EXPECT_EQ(blocks[0].language, Language::English);
    EXPECT_FLOAT_EQ(blocks[0].score, 0.9f);
}

TEST(MergeTest, JapaneseLinesJoinWithoutASpace) {
    const std::vector<OcrLine> lines = {horizontal(10, 100, 200, 20, "今から一人ずつ"),
                                        horizontal(10, 124, 180, 20, "俺に向かって")};

    const std::vector<TextBlock> blocks = mergeIntoBlocks(lines);

    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].text, "今から一人ずつ俺に向かって");
    EXPECT_EQ(blocks[0].language, Language::Japanese);
}

TEST(MergeTest, KoreanLinesJoinWithASpace) {
    const std::vector<OcrLine> lines = {horizontal(10, 100, 200, 20, "그전까진 신경"),
                                        horizontal(10, 124, 180, 20, "안 써서 몰랐는데")};

    const std::vector<TextBlock> blocks = mergeIntoBlocks(lines);

    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].text, "그전까진 신경 안 써서 몰랐는데");
}

TEST(MergeTest, EnglishHyphenAtTheEndOfALineIsJoinedBack) {
    const std::vector<OcrLine> lines = {horizontal(10, 100, 200, 20, "this is trans-"),
                                        horizontal(10, 124, 180, 20, "lation software")};

    const std::vector<TextBlock> blocks = mergeIntoBlocks(lines);

    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].text, "this is translation software");
}

TEST(MergeTest, HyphenThatIsPartOfAWordIsKept) {
    // 「well-known」本來就有連字號，不能吃掉
    EXPECT_EQ(joinLines(std::vector<std::string>{"a well-known", "example"}, Language::English),
              "a well-known example");
    // 行首不是字母時也不接回去
    EXPECT_EQ(joinLines(std::vector<std::string>{"page 3-", "4"}, Language::English), "page 3- 4");
}

TEST(MergeTest, FarApartLinesStaySeparate) {
    const std::vector<OcrLine> lines = {horizontal(10, 100, 200, 20, "first"),
                                        horizontal(10, 300, 200, 20, "second")};

    EXPECT_EQ(texts(mergeIntoBlocks(lines)), (std::vector<std::string>{"first", "second"}));
}

TEST(MergeTest, DifferentFontSizesStaySeparate) {
    // 標題和內文不該合併
    const std::vector<OcrLine> lines = {horizontal(10, 100, 200, 40, "Title"),
                                        horizontal(10, 148, 200, 16, "body text")};

    EXPECT_EQ(texts(mergeIntoBlocks(lines)), (std::vector<std::string>{"Title", "body text"}));
}

TEST(MergeTest, SideBySideColumnsStaySeparate) {
    // 兩欄並排：上下相鄰但左右不重疊，不能接成一句
    const std::vector<OcrLine> lines = {horizontal(10, 100, 100, 20, "left column"),
                                        horizontal(300, 124, 100, 20, "right column")};

    EXPECT_EQ(texts(mergeIntoBlocks(lines)).size(), 2u);
}

TEST(MergeTest, VerticalColumnsMergeRightToLeft) {
    const std::vector<OcrLine> lines = {vertical(60, 10, 20, 100, "向かって"),
                                        vertical(84, 10, 20, 100, "今から俺に")};

    const std::vector<TextBlock> blocks = mergeIntoBlocks(lines);

    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].text, "今から俺に向かって");
    EXPECT_EQ(blocks[0].orientation, Orientation::Vertical);
}

TEST(MergeTest, EmptyLinesAreIgnored) {
    const std::vector<OcrLine> lines = {horizontal(10, 100, 200, 20, ""),
                                        horizontal(10, 124, 180, 20, "text")};

    const std::vector<TextBlock> blocks = mergeIntoBlocks(lines);

    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].text, "text");
}

TEST(MergeTest, NoLinesGiveNoBlocks) {
    EXPECT_TRUE(mergeIntoBlocks({}).empty());
}

TEST(EdgeTest, BlocksTouchingTheEdgeAreDropped) {
    // 捲動網頁時，透鏡邊緣常常只拍到半句話
    const SizeI frame{400, 300};
    const std::vector<TextBlock> blocks = mergeIntoBlocks(
        std::vector<OcrLine>{horizontal(0, 100, 200, 20, "cut off on the left"),
                             horizontal(50, 150, 200, 20, "fully inside"),
                             horizontal(50, 285, 200, 20, "cut off at the bottom")});

    EXPECT_EQ(texts(dropEdgeBlocks(blocks, frame)), (std::vector<std::string>{"fully inside"}));
}

TEST(EdgeTest, DetectsEveryEdge) {
    const SizeI frame{400, 300};
    EXPECT_TRUE(touchesEdge(RectI::fromXYWH(1, 50, 100, 20), frame));
    EXPECT_TRUE(touchesEdge(RectI::fromXYWH(50, 1, 100, 20), frame));
    EXPECT_TRUE(touchesEdge(RectI::fromXYWH(298, 50, 100, 20), frame));   // 右邊界 398 ≥ 400-2
    EXPECT_TRUE(touchesEdge(RectI::fromXYWH(50, 278, 100, 20), frame));   // 下邊界 298 ≥ 300-2
    EXPECT_FALSE(touchesEdge(RectI::fromXYWH(297, 50, 100, 20), frame));  // 離邊界 3 像素：不算
    EXPECT_FALSE(touchesEdge(RectI::fromXYWH(50, 50, 100, 20), frame));
}

// 合併是「任意兩行相容就併成一群」，不是只看閱讀順序上相鄰的那兩行。
// 被擬聲詞或 ルビ 插隊一次就接不回來的話，日文漫畫的句子會碎成一片（design.md 4.4）。
TEST(MergeIntoBlocksTest, AnInterruptionDoesNotBreakTheRestOfTheChain) {
    const std::vector<OcrLine> lines{
        OcrLine{RectI{200, 50, 240, 250}, "ほんきで", 0.9f, Orientation::Vertical, {}},
        // 字大 3 倍的擬聲詞，在閱讀順序上排在兩欄本文之間
        OcrLine{RectI{195, 40, 315, 400}, "ドン", 0.9f, Orientation::Vertical, {}},
        OcrLine{RectI{150, 50, 190, 250}, "たたかうぞ", 0.9f, Orientation::Vertical, {}},
    };
    const std::vector<TextBlock> blocks = mergeIntoBlocks(lines);
    ASSERT_EQ(blocks.size(), 2u) << "擬聲詞自己一段，兩欄本文合成一段";
    bool merged = false;
    for (const TextBlock& block : blocks) {
        merged = merged || block.text == "ほんきでたたかうぞ";
    }
    EXPECT_TRUE(merged) << "兩欄本文要併在一起，不該被中間的擬聲詞切斷";
}

TEST(MergeIntoBlocksTest, VerticalColumnsAllowAWiderGap) {
    // 實測日文漫畫，被「欄距太遠」擋下的配對欄距中位數是字級的 1.57 倍
    const std::vector<OcrLine> lines{
        OcrLine{RectI{200, 50, 240, 250}, "ほんきで", 0.9f, Orientation::Vertical, {}},
        OcrLine{RectI{100, 50, 140, 250}, "たたかうぞ", 0.9f, Orientation::Vertical, {}},
    };
    ASSERT_EQ(mergeIntoBlocks(lines).size(), 1u) << "欄距 1.5 倍字級還算同一段";

    MergeOptions tight;
    tight.columnGapRatio = 0.8;
    EXPECT_EQ(mergeIntoBlocks(lines, tight).size(), 2u);
}

TEST(ResolveAmbiguousOrientationTest, ShortColumnsFollowTheMajority) {
    // 一兩個字的框接近正方形，從長寬比看不出方向
    std::vector<OcrLine> lines{
        OcrLine{RectI{200, 50, 240, 250}, "ほんきで", 0.9f, Orientation::Vertical, {}},
        OcrLine{RectI{150, 50, 190, 250}, "たたかうぞ", 0.9f, Orientation::Vertical, {}},
        OcrLine{RectI{100, 50, 140, 92}, "あ", 0.9f, Orientation::Horizontal, {}},
    };
    resolveAmbiguousOrientation(lines);
    EXPECT_EQ(lines[2].orientation, Orientation::Vertical) << "整頁是直排，看不出方向的就算直排";
    EXPECT_EQ(lines[0].orientation, Orientation::Vertical) << "方向明確的不要動";
}

TEST(ResolveAmbiguousOrientationTest, LeavesThingsAloneWithoutAMajority) {
    std::vector<OcrLine> lines{
        OcrLine{RectI{200, 50, 240, 250}, "たて", 0.9f, Orientation::Vertical, {}},
        OcrLine{RectI{100, 300, 400, 340}, "yoko", 0.9f, Orientation::Horizontal, {}},
        OcrLine{RectI{100, 50, 140, 92}, "あ", 0.9f, Orientation::Horizontal, {}},
    };
    resolveAmbiguousOrientation(lines);
    EXPECT_EQ(lines[2].orientation, Orientation::Horizontal) << "一比一，維持原樣";
}

TEST(MergeIntoBlocksTest, RubyPositionsMoveWithTheText) {
    // 接成一段之後，ルビ 的位置要換算成整段中的位置
    OcrLine first{RectI{200, 50, 240, 170}, "ほんき", 0.9f, Orientation::Vertical, {}};
    first.ruby.push_back(RubyAnnotation{0, 1, "マジ"});
    OcrLine second{RectI{150, 50, 190, 250}, "でたたかう", 0.9f, Orientation::Vertical, {}};
    second.ruby.push_back(RubyAnnotation{1, 2, "よみ"});

    const std::vector<OcrLine> lines{std::move(first), std::move(second)};
    const std::vector<TextBlock> blocks = mergeIntoBlocks(lines);
    ASSERT_EQ(blocks.size(), 1u);
    ASSERT_EQ(blocks[0].ruby.size(), 2u);
    EXPECT_EQ(blocks[0].ruby[0].start, 0);
    EXPECT_EQ(blocks[0].ruby[1].start, 1 + 3) << "第二行從第 3 個字開始（日文直接相連）";
}

}  // namespace
}  // namespace tmw::core
