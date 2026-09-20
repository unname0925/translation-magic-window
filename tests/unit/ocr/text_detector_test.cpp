#include "ocr/text_detector.h"

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

namespace tmw::ocr {
namespace {

cv::Size inputSize(int height, int width, DetectionOptions options = {}) {
    return detectionInputSize(height, width, options);
}

TEST(DetectionInputSizeTest, LargeImageKeepsSizeRoundedTo32) {
    // limit_type=min、limit_side_len=64：短邊夠長就不縮放，只對齊到 32 的倍數
    EXPECT_EQ(inputSize(405, 720), cv::Size(704, 416));
    EXPECT_EQ(inputSize(64, 64), cv::Size(64, 64));
}

TEST(DetectionInputSizeTest, RoundingIsHalfToEvenLikePython) {
    // 80 / 32 = 2.5 → Python 的 round 取偶數 2 → 64（四捨五入會是 96）
    EXPECT_EQ(inputSize(80, 208), cv::Size(192, 64));
    // 112 / 32 = 3.5 → 4 → 128
    EXPECT_EQ(inputSize(112, 112), cv::Size(128, 128));
}

TEST(DetectionInputSizeTest, SmallImageIsScaledUpToLimit) {
    // 短邊 20 < 64 → 放大 3.2 倍：高 64、寬 int(100 * 3.2) = 320
    EXPECT_EQ(inputSize(20, 100), cv::Size(320, 64));
}

TEST(DetectionInputSizeTest, MaxSideLimitShrinksHugeImages) {
    DetectionOptions options;
    options.maxSideLimit = 1000;
    // 2000 × 500 → 縮成 1000 × 250 → 250 / 32 = 7.8125 → 8 × 32 = 256
    EXPECT_EQ(inputSize(500, 2000, options), cv::Size(992, 256));
}

TEST(DetectionInputSizeTest, MaxLimitTypeShrinksLongSide) {
    DetectionOptions options;
    options.limitType = LimitType::Max;
    options.limitSideLen = 960;
    EXPECT_EQ(inputSize(1080, 1920, options), cv::Size(960, 544));
    // 長邊沒超過就不縮放；400 / 32 = 12.5 → 取偶數 12 → 384
    EXPECT_EQ(inputSize(300, 400, options), cv::Size(384, 288));
}

TEST(DetectionInputSizeTest, NeverSmallerThan32) {
    EXPECT_EQ(inputSize(1, 1), cv::Size(64, 64));
    EXPECT_THROW(inputSize(0, 10), std::invalid_argument);
}

TEST(MiniBoxTest, AxisAlignedRectangleIsOrderedClockwiseFromTopLeft) {
    const MiniBox box = miniBox(cv::RotatedRect(cv::Point2f(50, 20), cv::Size2f(80, 20), 0));
    EXPECT_EQ(box.points[0], cv::Point2f(10, 10));
    EXPECT_EQ(box.points[1], cv::Point2f(90, 10));
    EXPECT_EQ(box.points[2], cv::Point2f(90, 30));
    EXPECT_EQ(box.points[3], cv::Point2f(10, 30));
    EXPECT_FLOAT_EQ(box.shortSide, 20.0f);
}

TEST(MiniBoxTest, RotatedRectangleStartsFromLeftmostUpperCorner) {
    const MiniBox box = miniBox(cv::RotatedRect(cv::Point2f(100, 100), cv::Size2f(80, 20), 10));
    // 左邊兩點中 y 較小的是第一點，右邊兩點中 y 較小的是第二點
    EXPECT_LT(box.points[0].x, box.points[1].x);
    EXPECT_LT(box.points[0].y, box.points[3].y);
    EXPECT_LT(box.points[1].y, box.points[2].y);
    EXPECT_FLOAT_EQ(box.shortSide, 20.0f);
}

DetectedBox boxAt(int x, int y) {
    DetectedBox box;
    box.points = {cv::Point(x, y), cv::Point(x + 50, y), cv::Point(x + 50, y + 20),
                  cv::Point(x, y + 20)};
    return box;
}

TEST(SortBoxesTest, SameRowWithin10PixelsIsLeftToRight) {
    std::vector<DetectedBox> boxes = {boxAt(200, 105), boxAt(10, 100), boxAt(100, 108),
                                      boxAt(10, 150)};
    sortBoxes(boxes);
    // 先依 y 排成 (10,100)、(200,105)、(100,108)、(10,150)，
    // 再把同一行（y 差距 < 10）中 x 較小的往前換：(100,108) 換到 (200,105) 前面
    EXPECT_EQ(boxes[0].points[0], cv::Point(10, 100));
    EXPECT_EQ(boxes[1].points[0], cv::Point(100, 108));
    EXPECT_EQ(boxes[2].points[0], cv::Point(200, 105));
    EXPECT_EQ(boxes[3].points[0], cv::Point(10, 150));
}

TEST(SortBoxesTest, RowsMoreThan10PixelsApartKeepVerticalOrder) {
    std::vector<DetectedBox> boxes = {boxAt(10, 130), boxAt(300, 100)};
    sortBoxes(boxes);
    EXPECT_EQ(boxes[0].points[0], cv::Point(300, 100));
    EXPECT_EQ(boxes[1].points[0], cv::Point(10, 130));
}

TEST(BoxesFromProbabilityTest, RectangleBecomesOneExpandedBox) {
    cv::Mat probability = cv::Mat::zeros(64, 128, CV_32FC1);
    probability(cv::Rect(20, 20, 60, 16)).setTo(0.9f);
    const std::vector<DetectedBox> boxes = boxesFromProbability(probability, 256, 128, {});
    ASSERT_EQ(boxes.size(), 1u);
    EXPECT_NEAR(boxes[0].score, 0.9, 1e-6);
    // 擴張後再乘上 2 倍的縮放：比原本的 (40,40)-(158,70) 更大
    const Quad& q = boxes[0].points;
    EXPECT_LT(q[0].x, 40);
    EXPECT_LT(q[0].y, 40);
    EXPECT_GT(q[2].x, 158);
    EXPECT_GT(q[2].y, 70);
}

TEST(BoxesFromProbabilityTest, ThresholdIsComparedAsFloat) {
    // 機率剛好等於門檻時不算文字（pred > thresh）
    cv::Mat probability = cv::Mat::zeros(64, 128, CV_32FC1);
    probability(cv::Rect(20, 20, 60, 16)).setTo(0.3f);
    EXPECT_TRUE(boxesFromProbability(probability, 128, 64, {}).empty());
}

TEST(BoxesFromProbabilityTest, LowScoreAndTinyRegionsAreDropped) {
    cv::Mat probability = cv::Mat::zeros(64, 128, CV_32FC1);
    probability(cv::Rect(10, 10, 60, 16)).setTo(0.5f);  // 平均分數 0.5 < box_thresh 0.6
    probability(cv::Rect(100, 40, 2, 2)).setTo(0.95f);  // 短邊 2 < 3
    EXPECT_TRUE(boxesFromProbability(probability, 128, 64, {}).empty());
}

TEST(BoxesFromProbabilityTest, BoxesAreClampedToImage) {
    cv::Mat probability = cv::Mat::zeros(32, 64, CV_32FC1);
    probability(cv::Rect(0, 0, 40, 12)).setTo(0.99f);
    const std::vector<DetectedBox> boxes = boxesFromProbability(probability, 64, 32, {});
    ASSERT_EQ(boxes.size(), 1u);
    for (const cv::Point& p : boxes[0].points) {
        EXPECT_GE(p.x, 0);
        EXPECT_GE(p.y, 0);
        EXPECT_LE(p.x, 64);
        EXPECT_LE(p.y, 32);
    }
}

TEST(LetterboxLayoutTest, KeepsTheAspectRatioAndFitsInTheCanvas) {
    // 720×405 放進 1280×720：兩邊的比例相同，剛好填滿
    const LetterboxLayout layout = letterboxLayout(cv::Size(720, 405), cv::Size(1280, 720));
    EXPECT_NEAR(layout.scale, 1280.0 / 720.0, 1e-9);
    EXPECT_EQ(layout.resized, cv::Size(1280, 720));
}

TEST(LetterboxLayoutTest, ShrinksImagesLargerThanTheCanvas) {
    const LetterboxLayout layout = letterboxLayout(cv::Size(3840, 2160), cv::Size(1280, 720));
    EXPECT_NEAR(layout.scale, 1.0 / 3.0, 1e-9);
    EXPECT_EQ(layout.resized, cv::Size(1280, 720));
}

TEST(LetterboxLayoutTest, NeverExceedsTheCanvasOrDisappears) {
    for (const cv::Size size : {cv::Size(1, 1), cv::Size(1, 999), cv::Size(999, 1),
                                cv::Size(1279, 3), cv::Size(2001, 999)}) {
        const LetterboxLayout layout = letterboxLayout(size, cv::Size(1280, 720));
        EXPECT_GE(layout.resized.width, 1) << size;
        EXPECT_GE(layout.resized.height, 1) << size;
        EXPECT_LE(layout.resized.width, 1280) << size;
        EXPECT_LE(layout.resized.height, 720) << size;
    }
}

TEST(DetectionGridTest, RoundsUpToMultiplesOf32) {
    // 不是 32 的倍數時，模型內部的 Resize 對不齊，DirectML 會直接失敗
    EXPECT_EQ(roundUpToDetectionGrid(cv::Size(1280, 720)), cv::Size(1280, 736));
    EXPECT_EQ(roundUpToDetectionGrid(cv::Size(1280, 736)), cv::Size(1280, 736));
    EXPECT_EQ(roundUpToDetectionGrid(cv::Size(1, 1)), cv::Size(32, 32));
}

TEST(LetterboxLayoutTest, RejectsEmptySizes) {
    EXPECT_THROW(letterboxLayout(cv::Size(0, 10), cv::Size(1280, 720)), std::invalid_argument);
    EXPECT_THROW(letterboxLayout(cv::Size(10, 10), cv::Size(0, 720)), std::invalid_argument);
}

}  // namespace
}  // namespace tmw::ocr
