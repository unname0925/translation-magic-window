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
    // 一次辨識多行（DirectML 上逐行呼叫的固定成本很高，見 text_recognizer.h）。
    // 關掉時逐行辨識，結果和 PaddleOCR 官方版完全一致，用來做一致性檢查。
    bool batchRecognition = true;
    int maxBatch = 64;  // 一批最多幾行（實際張數由寬度預算決定，見 text_recognizer.h）
    // 建立時先空跑一次，把 DirectML 的編譯成本移到啟動階段（見 design.md 第 5 節）。
    // 偵測用 detection.fixedInput 的大小，辨識用最常見的寬度級距。
    bool warmUpOnStart = true;
};

class OcrPipeline {
public:
    OcrPipeline(const std::filesystem::path& detectionModelDir,
                const std::filesystem::path& recognitionModelDir, Device device,
                const OcrOptions& options = {});

    // bgr：CV_8UC3。timings 不是 nullptr 時填入各步驟的耗時。
    std::vector<TextLine> run(const cv::Mat& bgr, OcrTimings* timings = nullptr);

    const TextRecognizer& recognizer() const { return recognizer_; }

    // 實際使用的裝置（Auto 會解析成 Cpu 或 DirectML）
    Device device() const { return detector_.device(); }

private:
    // 用假的畫面各跑一次偵測和辨識。DirectML 第一次遇到一種形狀時要編譯（實測 227 ms），
    // 在這裡做完，使用者的第一次翻譯就不會卡住。
    void warmUp();

    OcrOptions options_;
    TextDetector detector_;
    TextRecognizer recognizer_;
};

}  // namespace tmw::ocr
