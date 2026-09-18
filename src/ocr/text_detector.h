#pragma once

#include <array>
#include <filesystem>
#include <opencv2/core.hpp>
#include <vector>

#include "ocr/model_config.h"
#include "ocr/onnx_model.h"

namespace tmw::ocr {

// PP-OCR 的文字偵測（DB）。前處理和後處理逐步對照 PaddleX 3.7 的實作
// （DetResizeForTest、NormalizeImage、DBPostProcess），同一張圖片的結果要和 Python 版一致。

enum class LimitType {
    Min,         // 短邊小於 limitSideLen 時才放大
    Max,         // 長邊大於 limitSideLen 時才縮小
    ResizeLong,  // 長邊一律縮放成 limitSideLen
};

// 預設值是 PaddleOCR 的 OCR 管線預設值（paddlex/configs/pipelines/OCR.yaml），
// 不是模型 inference.yml 裡的值。
struct DetectionOptions {
    int limitSideLen = 64;
    LimitType limitType = LimitType::Min;
    int maxSideLimit = 4000;
    float thresh = 0.3f;  // 機率圖的二值化門檻（PaddleX 以 float32 比較）
    double boxThresh = 0.6;
    double unclipRatio = 1.5;
    int maxCandidates = 1000;
};

// 四邊形文字框，依序是左上、右上、右下、左下（原圖座標）。
using Quad = std::array<cv::Point, 4>;

struct DetectedBox {
    Quad points;
    double score = 0.0;  // 框內機率的平均值
};

// 偵測模型的輸入大小：依 DetectionOptions 縮放後，寬高都對齊到 32 的倍數。
cv::Size detectionInputSize(int height, int width, const DetectionOptions& options);

// 最小外接矩形的四個角，排成左上、右上、右下、左下（PaddleX 的 get_mini_boxes），
// 以及矩形的短邊長度。
struct MiniBox {
    std::array<cv::Point2f, 4> points;
    float shortSide = 0.0f;
};
MiniBox miniBox(const cv::RotatedRect& rect);

// 機率圖 → 文字框（PaddleX 的 DBPostProcess，box_type=quad、score_mode=fast）。
// probability 是 CV_32F 的單通道機率圖；destWidth、destHeight 是原圖大小。
std::vector<DetectedBox> boxesFromProbability(const cv::Mat& probability, int destWidth,
                                              int destHeight, const DetectionOptions& options);

// 依照閱讀順序排序：由上到下；y 座標相差不到 10px 的視為同一行，由左到右
// （PaddleX 的 SortQuadBoxes）。
void sortBoxes(std::vector<DetectedBox>& boxes);

class TextDetector {
public:
    // modelDir 包含 inference.onnx 和 inference.yml。
    TextDetector(const std::filesystem::path& modelDir, Device device,
                 const DetectionOptions& options = {});

    // bgr：CV_8UC3。回傳偵測順序（尚未排序）的文字框。
    std::vector<DetectedBox> detect(const cv::Mat& bgr);

    // 前處理：縮放並正規化成 NCHW 的 float 資料。
    std::vector<float> preprocess(const cv::Mat& bgr, cv::Size& inputSize) const;

private:
    DetectionModelConfig config_;
    DetectionOptions options_;
    OnnxModel model_;
};

}  // namespace tmw::ocr
