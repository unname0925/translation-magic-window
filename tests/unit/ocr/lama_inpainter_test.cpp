// M4-01：LaMa 背景修補。取哪一塊送進模型；有模型時（models/lama，需要顯示卡）實際抹一次字。
#include "ocr/lama_inpainter.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>

namespace tmw::ocr {
namespace {

using core::ImageBgra;
using core::RectI;
using core::SizeI;

TEST(LamaCropTest, TakesASquareAroundTheTextWhenItFits) {
    const RectI crop = lamaCrop(SizeI{1000, 800}, RectI{400, 300, 500, 340}, 64);
    EXPECT_EQ(crop.width(), crop.height()) << "正方形，縮成 512×512 時不變形";
    EXPECT_EQ(crop.width(), 100 + 2 * 64);
    EXPECT_LE(crop.left, 400);
    EXPECT_GE(crop.right, 500);
}

TEST(LamaCropTest, StaysInsideTheFrame) {
    const RectI crop = lamaCrop(SizeI{1000, 800}, RectI{0, 760, 60, 800}, 64);
    EXPECT_EQ(crop.left, 0);
    EXPECT_EQ(crop.bottom, 800) << "碰到畫面邊就往內推";
    EXPECT_EQ(crop.width(), crop.height());
}

TEST(LamaCropTest, FallsBackToARectangleWhenNoSquareFits) {
    // 很寬的一行字放在很矮的透鏡裡：正方形放不下，取裁到畫面內的長方形
    const RectI crop = lamaCrop(SizeI{1000, 200}, RectI{100, 80, 900, 120}, 64);
    EXPECT_EQ(crop, (RectI{36, 16, 964, 184}));
}

TEST(LamaInpainterTest, AMissingModelGivesNothing) {
    LamaInpainter inpainter("C:/no/such/lama.onnx");
    const ImageBgra frame(100, 100);
    EXPECT_FALSE(inpainter.inpaint(frame, RectI{10, 10, 50, 30}).has_value());
    EXPECT_FALSE(inpainter.problem().empty()) << "記錄檔要寫得出為什麼沒有修補";
}

// 實際抹一次：灰底上一條黑線，抹掉之後應該是灰色
TEST(LamaInpainterTest, ErasesTextFromAPlainBackground) {
    const std::filesystem::path model = LamaInpainter::modelPath(TMW_MODELS_DIR);
    if (!std::filesystem::exists(model)) {
        GTEST_SKIP() << "沒有 " << model.string()
                     << "（tools/fetch_models --group inpaint，再執行 lama_for_directml.py）";
    }
    ImageBgra frame(300, 200);
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 300; ++x) {
            std::uint8_t* p = frame.pixel(x, y);
            const bool ink = y >= 95 && y < 105 && x >= 100 && x < 200;
            p[0] = p[1] = p[2] = ink ? 0 : 128;
            p[3] = 255;
        }
    }
    LamaInpainter inpainter(model);
    const RectI rect{95, 90, 205, 110};
    const auto patch = inpainter.inpaint(frame, rect);
    ASSERT_TRUE(patch.has_value()) << inpainter.problem();
    ASSERT_EQ(patch->width, rect.width());
    ASSERT_EQ(patch->height, rect.height());
    int far = 0;
    for (int y = 0; y < patch->height; ++y) {
        for (int x = 0; x < patch->width; ++x) {
            far += std::abs(patch->pixel(x, y)[1] - 128) > 30 ? 1 : 0;
        }
    }
    EXPECT_LT(far, patch->width * patch->height / 50) << "黑線抹掉了，補成周圍的灰色";
}

}  // namespace
}  // namespace tmw::ocr
