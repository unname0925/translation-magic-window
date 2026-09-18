#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace tmw::ocr {

// 從 PaddleOCR 模型資料夾的 inference.yml 讀出的設定（見 docs/design.md 4.4）。
// 讀取失敗或格式不符時丟出 std::runtime_error。

// 偵測模型：NormalizeImage 的參數。
struct DetectionModelConfig {
    std::array<double, 3> mean{0.485, 0.456, 0.406};
    std::array<double, 3> std{0.229, 0.224, 0.225};
    double scale = 1.0 / 255.0;
};

// 辨識模型：輸入大小和 CTC 字元表。
struct RecognitionModelConfig {
    int imageHeight = 48;  // RecResizeImg.image_shape[1]
    int imageWidth = 320;  // RecResizeImg.image_shape[2]：最小寬度
    // CTC 的字元表：索引 0 是 blank，接著是 inference.yml 的字元，最後是空白字元
    // （和 PaddleX 的 CTCLabelDecode 相同）。每個元素是一個 UTF-8 字元。
    std::vector<std::string> characters;
};

DetectionModelConfig loadDetectionModelConfig(const std::filesystem::path& inferenceYml);
RecognitionModelConfig loadRecognitionModelConfig(const std::filesystem::path& inferenceYml);

// 把 "1./255." 這種寫法（PaddleOCR 的 scale 欄位）轉成數值。
double parseScale(const std::string& text);

}  // namespace tmw::ocr
