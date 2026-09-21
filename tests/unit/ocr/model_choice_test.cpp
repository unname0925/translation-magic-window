// M1-03：依語言和裝置挑選模型（M0-11 的評測結果）。
#include "ocr/model_choice.h"

#include <gtest/gtest.h>

namespace tmw::ocr {
namespace {

TEST(ModelChoiceTest, UsesMediumOnTheGpu) {
    const ModelChoice choice = chooseModels(TextLanguage::JapaneseOrEnglish, Device::DirectML);
    EXPECT_EQ(choice.detection, "PP-OCRv6_medium_det");
    EXPECT_EQ(choice.recognition, "PP-OCRv6_medium_rec");
}

TEST(ModelChoiceTest, FallsBackToSmallWithoutAGpu) {
    // medium 在 CPU 上要 1.1 秒，small 只要 0.3 秒（design.md 第 5 節）
    const ModelChoice choice = chooseModels(TextLanguage::JapaneseOrEnglish, Device::Cpu);
    EXPECT_EQ(choice.detection, "PP-OCRv6_small_det");
    EXPECT_EQ(choice.recognition, "PP-OCRv6_small_rec");
}

TEST(ModelChoiceTest, KoreanKeepsTheSameDetectorButSwitchesRecognition) {
    // PP-OCRv6 不支援韓文，但它的偵測對韓文最好（M0-11）
    for (const Device device : {Device::DirectML, Device::Cpu}) {
        const ModelChoice korean = chooseModels(TextLanguage::Korean, device);
        const ModelChoice other = chooseModels(TextLanguage::JapaneseOrEnglish, device);
        EXPECT_EQ(korean.detection, other.detection);
        EXPECT_EQ(korean.recognition, "korean_PP-OCRv5_mobile_rec");
    }
}

TEST(ModelChoiceTest, AutoCountsAsAGpu) {
    // Auto 會先試 DirectML，所以先照 GPU 選；真的沒有 GPU 時由呼叫端用解析後的裝置再選一次
    EXPECT_EQ(chooseModels(TextLanguage::JapaneseOrEnglish, Device::Auto),
              chooseModels(TextLanguage::JapaneseOrEnglish, Device::DirectML));
}

TEST(ModelChoiceTest, BuildsPathsUnderTheModelsDirectory) {
    const ModelChoice choice = chooseModels(TextLanguage::Korean, Device::DirectML);
    EXPECT_EQ(detectionModelPath("C:/models", choice), "C:/models/PP-OCRv6_medium_det");
    EXPECT_EQ(recognitionModelPath("C:/models", choice), "C:/models/korean_PP-OCRv5_mobile_rec");
}

}  // namespace
}  // namespace tmw::ocr
