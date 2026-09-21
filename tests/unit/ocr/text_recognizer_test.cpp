#include "ocr/text_recognizer.h"

#include <gtest/gtest.h>

#include <vector>

namespace tmw::ocr {
namespace {

TEST(RecognitionInputWidthTest, ShortCropIsPaddedToMinimumWidth) {
    // 20×100 → 縮放成高 48：寬 ceil(48 × 5) = 240，補 0 到 320
    const RecognitionWidth width = recognitionInputWidth(20, 100, 48, 320);
    EXPECT_EQ(width.resizedWidth, 240);
    EXPECT_EQ(width.paddedWidth, 320);
}

TEST(RecognitionInputWidthTest, LongCropUsesItsOwnWidth) {
    // 30×1000：48 × 33.333… = 1600.0000000000002 → 補齊寬度 int() = 1600；
    // ceil 是 1601，超過補齊寬度，所以縮放寬度也是 1600
    const RecognitionWidth width = recognitionInputWidth(30, 1000, 48, 320);
    EXPECT_EQ(width.paddedWidth, 1600);
    EXPECT_EQ(width.resizedWidth, 1600);
}

TEST(RecognitionInputWidthTest, CeilNeverExceedsPaddedWidth) {
    // 7×100：48 × 100 / 7 = 685.71 → 補齊寬度 int() = 685，ceil = 686 → 取 685
    const RecognitionWidth width = recognitionInputWidth(7, 100, 48, 320);
    EXPECT_EQ(width.paddedWidth, 685);
    EXPECT_EQ(width.resizedWidth, 685);
}

TEST(RecognitionInputWidthTest, VeryLongCropIsCappedAt3200) {
    const RecognitionWidth width = recognitionInputWidth(10, 5000, 48, 320);
    EXPECT_EQ(width.paddedWidth, 3200);
    EXPECT_EQ(width.resizedWidth, 3200);
}

const std::vector<std::string> kCharacters = {"blank", "a", "b", "あ", " "};

// 每個時間點一列，value 放在 index 的位置，其餘平分剩下的機率
std::vector<float> steps(const std::vector<std::pair<size_t, float>>& sequence) {
    std::vector<float> probabilities;
    for (const auto& [index, value] : sequence) {
        std::vector<float> row(kCharacters.size(), (1.0f - value) / (kCharacters.size() - 1));
        row[index] = value;
        probabilities.insert(probabilities.end(), row.begin(), row.end());
    }
    return probabilities;
}

TEST(CtcGreedyDecodeTest, MergesRepeatsAndDropsBlanks) {
    // a a blank a b b あ → "aabあ"：blank 分開的兩個 a 都保留
    const auto p =
        steps({{1, 0.9f}, {1, 0.8f}, {0, 0.9f}, {1, 0.7f}, {2, 0.6f}, {2, 0.9f}, {3, 0.5f}});
    const Recognition result = ctcGreedyDecode(p, 7, kCharacters);
    EXPECT_EQ(result.text, "aabあ");
    // 分數是被選中的時間點（第 0、3、4、6 個）機率的平均
    EXPECT_NEAR(result.score, (0.9 + 0.7 + 0.6 + 0.5) / 4, 1e-6);
}

TEST(CtcGreedyDecodeTest, AllBlankGivesEmptyTextAndZeroScore) {
    const Recognition result = ctcGreedyDecode(steps({{0, 0.9f}, {0, 0.8f}}), 2, kCharacters);
    EXPECT_EQ(result.text, "");
    EXPECT_EQ(result.score, 0.0f);
}

TEST(CtcGreedyDecodeTest, TieTakesFirstIndexLikeNumpyArgmax) {
    std::vector<float> row = {0.1f, 0.4f, 0.4f, 0.05f, 0.05f};
    EXPECT_EQ(ctcGreedyDecode(row, 1, kCharacters).text, "a");
}

TEST(CtcGreedyDecodeTest, SizeMismatchThrows) {
    EXPECT_THROW(ctcGreedyDecode(std::vector<float>(7, 0.1f), 2, kCharacters),
                 std::invalid_argument);
}

TEST(CropTextRegionTest, HorizontalBoxIsCroppedToItsSize) {
    cv::Mat image(100, 200, CV_8UC3, cv::Scalar(255, 255, 255));
    const Quad box = {cv::Point(20, 30), cv::Point(120, 30), cv::Point(120, 50), cv::Point(20, 50)};
    const cv::Mat crop = cropTextRegion(image, box);
    EXPECT_EQ(crop.cols, 100);
    EXPECT_EQ(crop.rows, 20);
}

TEST(CropTextRegionTest, TallBoxIsRotatedCounterClockwise) {
    cv::Mat image(200, 100, CV_8UC3, cv::Scalar(0, 0, 0));
    // 上半部白、下半部黑的直條：逆時針轉 90 度後，白色在左邊
    image(cv::Rect(40, 20, 20, 60)).setTo(cv::Scalar(255, 255, 255));
    const Quad box = {cv::Point(40, 20), cv::Point(60, 20), cv::Point(60, 140), cv::Point(40, 140)};
    const cv::Mat crop = cropTextRegion(image, box);
    ASSERT_EQ(crop.rows, 20);
    ASSERT_EQ(crop.cols, 120);
    EXPECT_EQ(crop.at<cv::Vec3b>(10, 5)[0], 255);
    EXPECT_EQ(crop.at<cv::Vec3b>(10, 115)[0], 0);
}

TEST(RecognitionWidthBucketTest, RoundsUpToAFixedSetOfWidths) {
    // 級距要少（DirectML 每種輸入大小都要重新編譯），而且只能往上取（不能裁掉文字）
    EXPECT_EQ(recognitionWidthBucket(1), 160);
    EXPECT_EQ(recognitionWidthBucket(160), 160);
    EXPECT_EQ(recognitionWidthBucket(161), 256);
    EXPECT_EQ(recognitionWidthBucket(500), 512);
    EXPECT_EQ(recognitionWidthBucket(1025), 1600);
}

TEST(RecognitionWidthBucketTest, NeverExceedsTheModelInputLimit) {
    // PaddleX 的 max_imgW 是 3200，再寬也不能超過
    EXPECT_EQ(recognitionWidthBucket(3200), 3200);
    EXPECT_EQ(recognitionWidthBucket(9999), 3200);
}

TEST(RecognitionWidthBucketTest, IsNeverSmallerThanTheRequestedWidth) {
    for (int width = 1; width <= 3200; ++width) {
        ASSERT_GE(recognitionWidthBucket(width), width) << "寬度 " << width;
    }
}

TEST(RecognitionBatchSizeTest, SendsMoreNarrowCropsThanWideOnes) {
    // 一批的記憶體和寬度成正比，所以窄的圖一次可以送很多張
    EXPECT_GT(recognitionBatchSize(160, 64), recognitionBatchSize(1600, 64));
    EXPECT_LE(recognitionBatchSize(160, 64), 64);
    EXPECT_GE(recognitionBatchSize(3200, 64), 1);
}

TEST(RecognitionBatchSizeTest, RespectsTheUpperBoundAndNeverReturnsZero) {
    EXPECT_EQ(recognitionBatchSize(160, 4), 4);
    EXPECT_EQ(recognitionBatchSize(3200, 1), 1);
    EXPECT_GE(recognitionBatchSize(3200, 64), 1);
    EXPECT_THROW(recognitionBatchSize(0, 8), std::invalid_argument);
}

TEST(CropTextRegionTest, DegenerateBoxGivesEmptyCrop) {
    cv::Mat image(50, 50, CV_8UC3, cv::Scalar(0, 0, 0));
    const Quad box = {cv::Point(10, 10), cv::Point(10, 10), cv::Point(10, 10), cv::Point(10, 10)};
    EXPECT_TRUE(cropTextRegion(image, box).empty());
}

}  // namespace
}  // namespace tmw::ocr
