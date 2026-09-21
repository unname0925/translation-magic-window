#include "ocr/model_choice.h"

namespace tmw::ocr {

ModelChoice chooseModels(TextLanguage language, Device device) {
    // 沒有 GPU 時，medium 在 CPU 上要 1.1 秒，small 只要 0.3 秒（design.md 第 5 節）。
    // 代價是準確度：日文遊戲的字元錯誤率從 3.1% 變成 9.1%（M0-11）。
    const bool gpu = device == Device::DirectML || device == Device::Auto;
    ModelChoice choice;
    choice.detection = gpu ? "PP-OCRv6_medium_det" : "PP-OCRv6_small_det";
    choice.recognition = gpu ? "PP-OCRv6_medium_rec" : "PP-OCRv6_small_rec";
    if (language == TextLanguage::Korean) {
        // PP-OCRv6 不支援韓文；偵測仍用上面選好的（M0-11：v6 的偵測對韓文最好）
        choice.recognition = "korean_PP-OCRv5_mobile_rec";
    }
    return choice;
}

std::filesystem::path detectionModelPath(const std::filesystem::path& modelsDirectory,
                                         const ModelChoice& choice) {
    return modelsDirectory / choice.detection;
}

std::filesystem::path recognitionModelPath(const std::filesystem::path& modelsDirectory,
                                           const ModelChoice& choice) {
    return modelsDirectory / choice.recognition;
}

}  // namespace tmw::ocr
