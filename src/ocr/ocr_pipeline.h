#pragma once

#include <filesystem>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "ocr/onnx_model.h"
#include "ocr/text_detector.h"
#include "ocr/text_recognizer.h"

namespace tmw::ocr {

// 一張圖片的 OCR：偵測 → 依閱讀順序排序 → 裁切 → 辨識（PaddleX 的 OCR 管線，
// 不含文件方向校正、文件展平和文字行方向分類）。

struct TextLine {
    Quad box;               // 原圖座標
    std::string text;       // UTF-8
    float score = 0.0f;     // 辨識分數
    double boxScore = 0.0;  // 偵測分數
};

struct OcrTimings {
    double detectionMs = 0.0;    // 前處理 + 推論 + 後處理
    double recognitionMs = 0.0;  // 所有文字框的裁切 + 辨識
    int boxes = 0;               // 偵測到的文字框數
};

struct OcrOptions {
    DetectionOptions detection;
    float recognitionScoreThreshold = 0.0f;  // 分數低於這個值的結果捨棄
};

class OcrPipeline {
public:
    OcrPipeline(const std::filesystem::path& detectionModelDir,
                const std::filesystem::path& recognitionModelDir, Device device,
                const OcrOptions& options = {});

    // bgr：CV_8UC3。timings 不是 nullptr 時填入各步驟的耗時。
    std::vector<TextLine> run(const cv::Mat& bgr, OcrTimings* timings = nullptr);

    const TextRecognizer& recognizer() const { return recognizer_; }

private:
    OcrOptions options_;
    TextDetector detector_;
    TextRecognizer recognizer_;
};

}  // namespace tmw::ocr
