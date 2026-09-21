// 依語言和裝置挑選 OCR 模型（M0-11 的評測結果，見 docs/design.md 4.4）。
//
// - 日文、英文：PP-OCRv6 medium；沒有 GPU 時用 small（medium 在 CPU 上要 1.1 秒，small 0.3 秒）。
// - 韓文：偵測和上面相同，辨識用 korean_PP-OCRv5_mobile_rec（PP-OCRv6 不支援韓文）。
// - 日文漫畫的直排對白另外交給 manga-ocr（M2-03），不在這裡。
#pragma once

#include <filesystem>
#include <string>

#include "ocr/onnx_model.h"

namespace tmw::ocr {

enum class TextLanguage {
    JapaneseOrEnglish,
    Korean,
};

struct ModelChoice {
    std::string detection;  // models/ 底下的資料夾名稱
    std::string recognition;

    friend bool operator==(const ModelChoice&, const ModelChoice&) = default;
};

// device 是實際使用的裝置（Device::Auto 會被當成 DirectML，請傳入解析後的結果）。
ModelChoice chooseModels(TextLanguage language, Device device);

// modelsDirectory 底下的完整路徑
std::filesystem::path detectionModelPath(const std::filesystem::path& modelsDirectory,
                                         const ModelChoice& choice);
std::filesystem::path recognitionModelPath(const std::filesystem::path& modelsDirectory,
                                           const ModelChoice& choice);

}  // namespace tmw::ocr
