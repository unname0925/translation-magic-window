// UT-02：變化偵測（見 docs/execution-plan.md 5.3）
#include "core/change_detection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <stdexcept>

namespace tmw::core {
namespace {

GrayImage page() {
    // 模擬一頁文字的縮圖：白底，幾條深色的「文字行」
    GrayImage image(90, 50, 240);
    for (int row : {8, 16, 24}) {
        for (int x = 5; x < 80; ++x) {
            image.at(x, row) = 60;
        }
    }
    return image;
}

TEST(ChangeDetectionTest, IdenticalImagesAreUnchanged) {
    EXPECT_FALSE(contentChanged(page(), page()));
}

TEST(ChangeDetectionTest, SmallNoiseIsIgnored) {
    const GrayImage before = page();
    GrayImage after = before;
    std::mt19937 random(42);
    std::uniform_int_distribution<int> noise(-4, 4);
    for (auto& pixel : after.pixels) {
        pixel = static_cast<std::uint8_t>(std::clamp(pixel + noise(random), 0, 255));
    }
    EXPECT_FALSE(contentChanged(before, after));
}

TEST(ChangeDetectionTest, NewTextLineIsDetected) {
    const GrayImage before = page();
    GrayImage after = before;
    for (int x = 5; x < 40; ++x) {
        after.at(x, 32) = 60;
    }
    EXPECT_TRUE(contentChanged(before, after));
}

TEST(ChangeDetectionTest, SingleNewCharacterIsDetected) {
    // 打字機效果：多出一個字，在縮圖上大約只佔 1～2 個像素
    const GrayImage before = page();
    GrayImage after = before;
    after.at(82, 24) = 90;
    EXPECT_TRUE(contentChanged(before, after));
}

TEST(ChangeDetectionTest, GlobalFadeIsDetected) {
    // 每個像素只變一點點（低於單一像素的門檻），但整體明顯變暗
    const GrayImage before = page();
    GrayImage after = before;
    for (auto& pixel : after.pixels) {
        pixel = static_cast<std::uint8_t>(pixel - 8);
    }
    EXPECT_EQ(measureChange(before, after, 12).changedPixels, 0);
    EXPECT_TRUE(contentChanged(before, after));
}

TEST(ChangeDetectionTest, DifferentSizeCountsAsChanged) {
    EXPECT_TRUE(contentChanged(GrayImage(90, 50, 240), GrayImage(91, 50, 240)));
}

TEST(ChangeDetectionTest, ThresholdsAreConfigurable) {
    const GrayImage before = page();
    GrayImage after = before;
    after.at(1, 1) = 200;  // 一個像素差 40
    ChangeThresholds tolerant;
    tolerant.minChangedPixels = 3;
    EXPECT_TRUE(contentChanged(before, after));
    EXPECT_FALSE(contentChanged(before, after, tolerant));
}

TEST(ChangeDetectionTest, MeasureChangeReportsMetrics) {
    GrayImage before(10, 10, 100);
    GrayImage after = before;
    after.at(0, 0) = 150;
    after.at(1, 0) = 105;
    const ChangeMetrics metrics = measureChange(before, after, 12);
    EXPECT_EQ(metrics.changedPixels, 1);
    EXPECT_DOUBLE_EQ(metrics.meanAbsDelta, 55.0 / 100.0);
}

TEST(ChangeDetectionTest, MeasureChangeRejectsDifferentSizes) {
    EXPECT_THROW(measureChange(GrayImage(2, 2), GrayImage(3, 2), 12), std::invalid_argument);
}

TEST(ToGrayTest, UsesLuminanceWeights) {
    ImageBgra image(4, 1);
    const std::uint8_t colors[4][3] = {{0, 0, 0}, {255, 255, 255}, {0, 255, 0}, {255, 0, 0}};
    for (int x = 0; x < 4; ++x) {
        std::uint8_t* p = image.pixel(x, 0);
        p[0] = colors[x][0];  // B
        p[1] = colors[x][1];  // G
        p[2] = colors[x][2];  // R
        p[3] = 255;
    }
    const GrayImage gray = toGray(image);
    EXPECT_EQ(gray.at(0, 0), 0);
    EXPECT_EQ(gray.at(1, 0), 255);
    EXPECT_EQ(gray.at(2, 0), 149);  // 純綠最亮：(150 × 255 + 128) >> 8
    EXPECT_EQ(gray.at(3, 0), 29);   // 純藍最暗
}

}  // namespace
}  // namespace tmw::core
