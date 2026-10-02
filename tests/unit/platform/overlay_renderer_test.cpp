// M3-03、M3-05：譯文畫出來的樣子。不比對整張圖（字型的版本不同，每個像素都可能差一點），
// 改檢查看得出好壞的性質：底色、字在框內、直排是直的、字級跟著框走。
#include "platform/overlay_renderer.h"

#include <gtest/gtest.h>

#include <vector>

namespace tmw::platform {
namespace {

using core::ImageBgra;
using core::OverlayItem;
using core::RectI;
using core::Rgba;

OverlayItem item(RectI rect, std::string text, bool vertical = false) {
    OverlayItem out;
    out.rect = rect;
    out.text = std::move(text);
    out.vertical = vertical;
    out.background = Rgba{255, 255, 255, 255};
    out.foreground = Rgba{0, 0, 0, 255};
    return out;
}

// 有墨水的像素（不透明而且比白底暗）的外框
RectI inkBounds(const ImageBgra& image, const RectI& within) {
    RectI ink{within.right, within.bottom, within.left, within.top};
    for (int y = within.top; y < within.bottom; ++y) {
        for (int x = within.left; x < within.right; ++x) {
            const std::uint8_t* p = image.pixel(x, y);
            if (p[3] == 255 && p[1] < 128) {
                ink.left = std::min(ink.left, x);
                ink.top = std::min(ink.top, y);
                ink.right = std::max(ink.right, x + 1);
                ink.bottom = std::max(ink.bottom, y + 1);
            }
        }
    }
    return ink;
}

class OverlayRendererTest : public ::testing::Test {
protected:
    OverlayRenderer renderer_;
};

TEST_F(OverlayRendererTest, OnlyTheTranslatedPartsAreOpaque) {
    const std::vector<OverlayItem> items{item(RectI{10, 10, 110, 40}, "存檔")};
    const ImageBgra image = renderer_.render(core::SizeI{200, 100}, items);
    ASSERT_EQ(image.width, 200);
    EXPECT_EQ(image.pixel(150, 80)[3], 0) << "框外完全透明，看得到底下的畫面";
    EXPECT_EQ(image.pixel(12, 12)[3], 255) << "框內不透明，蓋住原文";
    EXPECT_EQ(image.pixel(12, 12)[2], 255) << "用背景色填滿";
}

TEST_F(OverlayRendererTest, TheTextStaysInsideItsBox) {
    const RectI box{20, 20, 140, 80};
    const std::vector<OverlayItem> items{
        item(box, "這是一段比較長的譯文，要自動換行而且縮小字級才放得下")};
    const ImageBgra image = renderer_.render(core::SizeI{200, 120}, items);
    const RectI whole{0, 0, 200, 120};
    const RectI ink = inkBounds(image, whole);
    ASSERT_FALSE(ink.empty()) << "有畫出字";
    EXPECT_GE(ink.left, box.left);
    EXPECT_GE(ink.top, box.top);
    EXPECT_LE(ink.right, box.right);
    EXPECT_LE(ink.bottom, box.bottom);
    EXPECT_GT(ink.height(), box.height() / 2) << "縮到剛好放得下，不是縮成一小塊";
}

TEST_F(OverlayRendererTest, VerticalTextRunsDownTheColumn) {
    const RectI box{40, 10, 80, 190};
    const std::vector<OverlayItem> items{item(box, "你要去哪裡", true)};
    const ImageBgra image = renderer_.render(core::SizeI{120, 200}, items);
    const RectI ink = inkBounds(image, RectI{0, 0, 120, 200});
    ASSERT_FALSE(ink.empty());
    EXPECT_GT(ink.height(), ink.width() * 3) << "五個字排成一直欄";
    EXPECT_GE(ink.left, box.left);
    EXPECT_LE(ink.right, box.right);
    EXPECT_GE(ink.top, box.top);
    EXPECT_LE(ink.bottom, box.bottom);
}

TEST_F(OverlayRendererTest, VerticalColumnsFlowRightToLeft) {
    // 放不進一欄：第一欄在右邊
    OverlayItem column = item(RectI{0, 0, 80, 80}, "一二三四五六七八九十");
    column.vertical = true;
    column.lineThickness = 20;
    const std::vector<OverlayItem> items{column};
    const ImageBgra image = renderer_.render(core::SizeI{80, 80}, items);
    // 第一個字「一」是一條橫線：在最右邊那一欄的最上面
    const RectI ink = inkBounds(image, RectI{0, 0, 80, 80});
    ASSERT_FALSE(ink.empty());
    const RectI firstColumn = inkBounds(image, RectI{ink.right - 18, 0, 80, 80});
    EXPECT_LT(firstColumn.top, ink.top + 10) << "右邊那欄從最上面開始";
    EXPECT_GT(ink.width(), 30) << "排成好幾欄";
}

TEST_F(OverlayRendererTest, BiggerBoxesGetBiggerText) {
    const float small = renderer_.fitFontSize(item(RectI{0, 0, 100, 30}, "存檔"));
    const float large = renderer_.fitFontSize(item(RectI{0, 0, 300, 90}, "存檔"));
    EXPECT_GT(large, small);
    EXPECT_GE(small, OverlayRenderer::kMinFontSize);
}

TEST_F(OverlayRendererTest, ShortTranslationsAreNotBlownUp) {
    OverlayItem big = item(RectI{0, 0, 400, 300}, "好");
    big.lineThickness = 24;
    EXPECT_LE(renderer_.fitFontSize(big), 24.0f) << "不比原文的字大";
}

TEST_F(OverlayRendererTest, TinyBoxesFallBackToTheSmallestSize) {
    EXPECT_EQ(renderer_.fitFontSize(item(RectI{0, 0, 12, 6}, "這段譯文怎樣都放不下")),
              OverlayRenderer::kMinFontSize);
}

// 沿著一個方向，有墨水的那幾段（連續的列或欄）：[起點, 終點)
std::vector<std::pair<int, int>> inkBands(const ImageBgra& image, bool columns) {
    const int length = columns ? image.width : image.height;
    const int across = columns ? image.height : image.width;
    std::vector<std::pair<int, int>> bands;
    for (int i = 0; i < length; ++i) {
        bool inked = false;
        for (int j = 0; j < across && !inked; ++j) {
            const std::uint8_t* p = columns ? image.pixel(i, j) : image.pixel(j, i);
            inked = p[3] == 255 && p[1] < 128;
        }
        if (inked && (bands.empty() || bands.back().second != i)) {
            bands.emplace_back(i, i + 1);
        } else if (inked) {
            bands.back().second = i + 1;
        }
    }
    return bands;
}

// M3-03：另有含義的ルビ，譯文也用小字畫在詞旁邊
TEST_F(OverlayRendererTest, RubyIsDrawnSmallAboveHorizontalText) {
    OverlayItem line = item(RectI{0, 0, 300, 80}, "我要認真打一場");
    line.ruby = {{2, 2, "玩真的"}};
    line.lineThickness = 30;
    const std::vector<OverlayItem> items{line};
    const auto bands = inkBands(renderer_.render(core::SizeI{300, 80}, items), false);
    ASSERT_EQ(bands.size(), 2u) << "上面一帶是ルビ，下面一帶是正文";
    EXPECT_LT(bands[0].second - bands[0].first, bands[1].second - bands[1].first)
        << "ルビ的字比較小";
}

TEST_F(OverlayRendererTest, RubyIsDrawnSmallRightOfVerticalText) {
    OverlayItem column = item(RectI{0, 0, 80, 300}, "我要認真打一場", true);
    column.ruby = {{2, 2, "玩真的"}};
    column.lineThickness = 30;
    const std::vector<OverlayItem> items{column};
    const auto bands = inkBands(renderer_.render(core::SizeI{80, 300}, items), true);
    ASSERT_EQ(bands.size(), 2u) << "左邊一帶是正文，右邊一帶是ルビ";
    EXPECT_GT(bands[0].second - bands[0].first, bands[1].second - bands[1].first)
        << "ルビ在右邊，字比較小";
}

TEST_F(OverlayRendererTest, OutlinedTextHasBothColours) {
    // M4-02：白字黑邊（遊戲常見）畫在藍底上
    OverlayItem outlined = item(RectI{0, 0, 200, 60}, "存檔");
    outlined.background = Rgba{60, 110, 200, 255};
    outlined.foreground = Rgba{255, 255, 255, 255};
    outlined.outline = Rgba{0, 0, 0, 255};
    outlined.lineThickness = 40;
    const std::vector<OverlayItem> items{outlined};
    const ImageBgra image = renderer_.render(core::SizeI{200, 60}, items);
    int white = 0;
    int black = 0;
    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 200; ++x) {
            const std::uint8_t* p = image.pixel(x, y);
            white += p[0] > 230 && p[1] > 230 && p[2] > 230 ? 1 : 0;
            black += p[0] < 25 && p[1] < 25 && p[2] < 25 ? 1 : 0;
        }
    }
    EXPECT_GT(white, 30) << "填色";
    EXPECT_GT(black, 30) << "描邊";
}

// M4-04：內建字型從檔案載入（tools/fetch_models 下載到 models/fonts）。檔案不在時略過
TEST_F(OverlayRendererTest, UsesABuiltInFontFromItsFile) {
    const std::filesystem::path file =
        std::filesystem::path(TMW_MODELS_DIR) / "fonts" / "jf-openhuninn-2.1.ttf";
    if (!std::filesystem::exists(file)) {
        GTEST_SKIP() << "沒有 " << file.string();
    }
    const std::vector<OverlayItem> items{item(RectI{0, 0, 120, 60}, "做什麼")};
    const ImageBgra system = renderer_.render(core::SizeI{120, 60}, items);

    ASSERT_TRUE(renderer_.setFont(file));
    EXPECT_FALSE(renderer_.fontFamily().empty());
    EXPECT_NE(renderer_.render(core::SizeI{120, 60}, items).pixels, system.pixels)
        << "換了字型，畫出來要不一樣";

    ASSERT_TRUE(renderer_.setFont({}));
    EXPECT_EQ(renderer_.render(core::SizeI{120, 60}, items).pixels, system.pixels)
        << "空路徑換回微軟正黑體";
}

TEST_F(OverlayRendererTest, AMissingFontFileFallsBackToTheSystemFont) {
    EXPECT_FALSE(renderer_.setFont("C:/no/such/font.ttf"));
    EXPECT_TRUE(renderer_.fontFamily().empty());
    const std::vector<OverlayItem> items{item(RectI{0, 0, 120, 60}, "做什麼")};
    EXPECT_FALSE(inkBands(renderer_.render(core::SizeI{120, 60}, items), false).empty())
        << "照樣畫得出字";
}

TEST_F(OverlayRendererTest, LightTextOnDarkBackgrounds) {
    OverlayItem dark = item(RectI{0, 0, 100, 40}, "危險");
    dark.background = Rgba{20, 20, 20, 255};
    dark.foreground = Rgba{255, 255, 255, 255};
    const std::vector<OverlayItem> items{dark};
    const ImageBgra image = renderer_.render(core::SizeI{100, 40}, items);
    int bright = 0;
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 100; ++x) {
            bright += image.pixel(x, y)[1] > 200 ? 1 : 0;
        }
    }
    EXPECT_GT(bright, 20) << "白字畫在深色底上";
    EXPECT_EQ(image.pixel(1, 1)[1], 20);
}

}  // namespace
}  // namespace tmw::platform
