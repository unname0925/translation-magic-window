#pragma once

#include <filesystem>
#include <opencv2/core.hpp>
#include <optional>
#include <string>
#include <vector>

#include "core/language.h"
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

// 產品用的偵測輸入大小。透鏡可以調整大小，但模型的輸入形狀必須固定：DirectML 上
// 一個工作階段只有「第一次看到的大小」跑得快，之後每換一種大小，那個大小的每一次推論
// 都永久慢 3～5 倍（M1-03，design.md 第 5 節）。暖機和產品都用這個值，才不會各用各的。
cv::Size lensDetectionInput();

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

struct OcrRun {
    std::vector<TextLine> lines;
    // 實際採用的辨識模型：Korean，或主模型的 Japanese／English。
    // 沒有載入韓文模型時永遠是主模型那一邊。
    core::Language script = core::Language::Unknown;
};

class OcrPipeline {
public:
    OcrPipeline(const std::filesystem::path& detectionModelDir,
                const std::filesystem::path& recognitionModelDir, Device device,
                const OcrOptions& options = {});

    // 多載入一個韓文辨識模型（PP-OCRv6 不支援韓文，見 design.md 4.4）。
    // koreanRecognitionModelDir 是空路徑時，行為和上面那個建構子一樣。
    OcrPipeline(const std::filesystem::path& detectionModelDir,
                const std::filesystem::path& recognitionModelDir,
                const std::filesystem::path& koreanRecognitionModelDir, Device device,
                const OcrOptions& options = {});

    // bgr：CV_8UC3。timings 不是 nullptr 時填入各步驟的耗時。只用主模型。
    std::vector<TextLine> run(const cv::Mat& bgr, OcrTimings* timings = nullptr);

    // script 是 Unknown 時兩個辨識模型都跑，再整張一起決定用哪一邊（core::chooseScript）；
    // 指定 Korean 或 Japanese／English 就只跑那一個，省掉一半的辨識時間。
    OcrRun run(const cv::Mat& bgr, core::Language script, OcrTimings* timings = nullptr);

    const TextRecognizer& recognizer() const { return recognizer_; }

    // 有沒有載入韓文模型
    bool hasKoreanModel() const { return korean_.has_value(); }

    // 實際使用的裝置（Auto 會解析成 Cpu 或 DirectML）
    Device device() const { return detector_.device(); }

private:
    // 用假的畫面各跑一次偵測和辨識。DirectML 第一次遇到一種形狀時要編譯（實測 227 ms），
    // 在這裡做完，使用者的第一次翻譯就不會卡住。
    void warmUp();

    // 用某一個辨識模型讀出所有裁切圖的文字
    std::vector<TextLine> recognizeCrops(TextRecognizer& recognizer,
                                         const std::vector<DetectedBox>& boxes,
                                         const std::vector<cv::Mat>& crops);

    OcrOptions options_;
    TextDetector detector_;
    TextRecognizer recognizer_;
    // 韓文辨識模型（PP-OCRv6 不支援韓文）。沒載入時只用主模型。
    std::optional<TextRecognizer> korean_;
};

}  // namespace tmw::ocr
