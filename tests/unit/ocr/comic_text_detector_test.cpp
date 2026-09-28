// comic-text-detector 的後處理（M2-02）。不需要模型：直接餵候選框，看解出來的區塊對不對。
#include "ocr/comic_text_detector.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

namespace tmw::ocr {
namespace {

// 一個候選框：中心、寬高（1024 輸入的座標）、物件分數、兩個類別分數
void add(std::vector<float>& out, float cx, float cy, float w, float h, float objectness,
         float class0, float class1 = 0.0f) {
    out.insert(out.end(), {cx, cy, w, h, objectness, class0, class1});
}

TEST(DecodeComicTextBlocksTest, TurnsACandidateIntoABoxOnTheOriginalImage) {
    std::vector<float> candidates;
    add(candidates, 200, 100, 100, 50, 0.9f, 1.0f);
    // 原圖 2048×1024 縮成 1024×512：倍數 0.5，座標要乘回 2
    const auto blocks = decodeComicTextBlocks(candidates, 0.5, {2048, 1024});
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].rect, (core::RectI{300, 150, 500, 250}));
    EXPECT_FLOAT_EQ(blocks[0].score, 0.9f);
}

TEST(DecodeComicTextBlocksTest, ScoreIsObjectnessTimesClass) {
    // 物件分數很高、類別分數很低：乘起來 0.9×0.3=0.27，過不了 0.4 的門檻
    std::vector<float> candidates;
    add(candidates, 200, 100, 100, 50, 0.9f, 0.3f);
    EXPECT_TRUE(decodeComicTextBlocks(candidates, 1.0, {1024, 1024}).empty());
}

TEST(DecodeComicTextBlocksTest, PicksTheBetterClass) {
    std::vector<float> candidates;
    add(candidates, 200, 100, 100, 50, 1.0f, 0.5f, 0.8f);
    const auto blocks = decodeComicTextBlocks(candidates, 1.0, {1024, 1024});
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].label, 1);
    EXPECT_FLOAT_EQ(blocks[0].score, 0.8f);
}

TEST(DecodeComicTextBlocksTest, NmsKeepsTheBestOfOverlappingBoxes) {
    std::vector<float> candidates;
    add(candidates, 200, 200, 100, 100, 1.0f, 0.6f);  // 分數低
    add(candidates, 205, 205, 100, 100, 1.0f, 0.9f);  // 幾乎同一個框、分數高
    const auto blocks = decodeComicTextBlocks(candidates, 1.0, {1024, 1024});
    ASSERT_EQ(blocks.size(), 1u) << "同一個對話框只能留一個";
    EXPECT_FLOAT_EQ(blocks[0].score, 0.9f);
}

TEST(DecodeComicTextBlocksTest, NmsKeepsBoxesThatDoNotOverlapMuch) {
    std::vector<float> candidates;
    add(candidates, 200, 200, 100, 100, 1.0f, 0.9f);
    add(candidates, 600, 600, 100, 100, 1.0f, 0.8f);
    EXPECT_EQ(decodeComicTextBlocks(candidates, 1.0, {1024, 1024}).size(), 2u);
}

TEST(DecodeComicTextBlocksTest, ClipsBoxesToTheImage) {
    // 框超出原圖（補 0 的區域、或畫面邊緣被切到的對話框）
    std::vector<float> candidates;
    add(candidates, 20, 20, 100, 100, 1.0f, 0.9f);
    const auto blocks = decodeComicTextBlocks(candidates, 1.0, {50, 50});
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].rect, (core::RectI{0, 0, 50, 50}));
}

TEST(DecodeComicTextBlocksTest, DropsBoxesEntirelyInThePadding) {
    // 原圖只有 512×512，框整個落在右邊補 0 的區域：裁完是空的，丟掉
    std::vector<float> candidates;
    add(candidates, 900, 100, 50, 50, 1.0f, 0.9f);
    EXPECT_TRUE(decodeComicTextBlocks(candidates, 1.0, {512, 512}).empty());
}

TEST(DecodeComicTextBlocksTest, RoundsTheFarEdgeHalfToEven) {
    // 參考實作用 Python 的 round()：2.5 → 2、3.5 → 4
    std::vector<float> candidates;
    add(candidates, 1.5f, 1.5f, 2.0f, 2.0f, 1.0f, 0.9f);      // 右下 2.5
    add(candidates, 500.5f, 500.5f, 6.0f, 6.0f, 1.0f, 0.9f);  // 右下 503.5
    const auto blocks = decodeComicTextBlocks(candidates, 1.0, {1024, 1024});
    ASSERT_EQ(blocks.size(), 2u);
    EXPECT_EQ(blocks[0].rect.right, 2);
    EXPECT_EQ(blocks[1].rect.right, 504);
}

TEST(DecodeComicTextBlocksTest, RejectsMalformedInput) {
    const std::vector<float> notMultipleOfSeven(10, 0.0f);
    EXPECT_THROW(decodeComicTextBlocks(notMultipleOfSeven, 1.0, {100, 100}), std::invalid_argument);
    const std::vector<float> one(7, 0.0f);
    EXPECT_THROW(decodeComicTextBlocks(one, 0.0, {100, 100}), std::invalid_argument);
}

TEST(DecodeComicTextBlocksTest, NothingInNothingOut) {
    EXPECT_TRUE(decodeComicTextBlocks({}, 1.0, {100, 100}).empty());
}

}  // namespace
}  // namespace tmw::ocr
