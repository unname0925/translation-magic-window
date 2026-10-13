// 雜誌廣告頁不翻：字多、段多，而且橫排為主、彩色、或不在對話框裡（core/page_kind.h）。
#include "core/page_kind.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace tmw::core {
namespace {

ImageBgra frame(std::uint8_t b, std::uint8_t g, std::uint8_t r) {
    ImageBgra image(100, 140);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            std::uint8_t* p = image.pixel(x, y);
            p[0] = b;
            p[1] = g;
            p[2] = r;
            p[3] = 255;
        }
    }
    return image;
}

// count 段，每段 characters 個字
std::vector<TextBlock> blocks(int count, int characters, Orientation orientation, bool inBubble) {
    std::vector<TextBlock> out(static_cast<std::size_t>(count));
    std::string text;
    for (int i = 0; i < characters; ++i) {
        text += "字";
    }
    for (TextBlock& block : out) {
        block.text = text;
        block.orientation = orientation;
        block.inBubble = inBubble;
    }
    return out;
}

TEST(PageKindTest, DenseHorizontalTextIsAnAd) {
    const ImageBgra white = frame(255, 255, 255);
    const auto ad = blocks(20, 20, Orientation::Horizontal, true);
    const PageTextStats stats = pageTextStats(white, ad);
    EXPECT_EQ(stats.segments, 20);
    EXPECT_EQ(stats.characters, 400);
    EXPECT_DOUBLE_EQ(stats.horizontalShare, 1.0);
    EXPECT_TRUE(looksLikeAdPage(stats));
}

TEST(PageKindTest, ColouredOrOutsideBubblesIsAnAdEvenWhenVertical) {
    const auto vertical = blocks(20, 20, Orientation::Vertical, true);
    const ImageBgra pink = frame(160, 40, 230);  // 粉紅雙色印刷
    EXPECT_GT(pageTextStats(pink, vertical).colorShare, 0.9);
    EXPECT_TRUE(looksLikeAdPage(pageTextStats(pink, vertical)));
    const auto loose = blocks(20, 20, Orientation::Vertical, false);
    EXPECT_TRUE(looksLikeAdPage(pageTextStats(frame(255, 255, 255), loose)))
        << "對話框外的直排小字（告示、投稿募集）";
}

TEST(PageKindTest, MangaPagesAreNotAds) {
    const ImageBgra white = frame(255, 255, 255);
    EXPECT_FALSE(looksLikeAdPage(pageTextStats(white, blocks(8, 10, Orientation::Vertical, true))))
        << "一般的漫畫頁";
    EXPECT_FALSE(looksLikeAdPage(pageTextStats(white, blocks(20, 20, Orientation::Vertical, true))))
        << "字很多、但直排、黑白、在對話框裡";
    EXPECT_FALSE(
        looksLikeAdPage(pageTextStats(white, blocks(10, 40, Orientation::Horizontal, false))))
        << "段數不夠多";
    EXPECT_FALSE(looksLikeAdPage(
        pageTextStats(frame(160, 40, 230), blocks(6, 5, Orientation::Vertical, true))))
        << "字很少的彩頁";
}

}  // namespace
}  // namespace tmw::core
