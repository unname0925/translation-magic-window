#pragma once

#include <filesystem>
#include <opencv2/core.hpp>
#include <span>
#include <string>
#include <vector>

#include "ocr/model_config.h"
#include "ocr/onnx_model.h"
#include "ocr/text_detector.h"

namespace tmw::ocr {

// PP-OCR 的文字辨識（CTC）。前處理和解碼逐步對照 PaddleX 3.7 的實作
// （OCRReisizeNormImg、CTCLabelDecode），一次辨識一張裁切圖（batch size 1）。

struct Recognition {
    std::string text;    // UTF-8
    float score = 0.0f;  // 選中字元機率的平均值；沒有字元時是 0
};

// 辨識模型的輸入寬度。resizedWidth 是圖片縮放後的寬度，paddedWidth 是補 0 之後的寬度。
struct RecognitionWidth {
    int resizedWidth = 0;
    int paddedWidth = 0;
};
RecognitionWidth recognitionInputWidth(int cropHeight, int cropWidth, int imageHeight,
                                       int minimumWidth);

// CTC 貪婪解碼：每個時間點取機率最大的類別，合併連續重複的類別，再去掉 blank（索引 0）。
// probabilities 是 timeSteps × characters.size() 的機率。
Recognition ctcGreedyDecode(std::span<const float> probabilities, int timeSteps,
                            const std::vector<std::string>& characters);

// 依照文字框從原圖裁出文字，拉正成水平的長條（PaddleX 的 CropByPolys，quad 模式）。
// 高度是寬度的 1.5 倍以上時逆時針旋轉 90 度。文字框太小裁不出來時回傳空的 Mat。
cv::Mat cropTextRegion(const cv::Mat& bgr, const Quad& box);

class TextRecognizer {
public:
    // modelDir 包含 inference.onnx 和 inference.yml。
    TextRecognizer(const std::filesystem::path& modelDir, Device device);

    // crop：CV_8UC3 的裁切圖。
    Recognition recognize(const cv::Mat& crop);

    const RecognitionModelConfig& config() const { return config_; }

private:
    RecognitionModelConfig config_;
    OnnxModel model_;
};

}  // namespace tmw::ocr
