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
// （OCRReisizeNormImg、CTCLabelDecode）。
//
// 一張一張辨識（recognize(crop)）和官方版完全一致，但 DirectML 上每次呼叫的固定成本很高
// （M0-14 實測每行 25～40 ms，和模型大小幾乎無關），所以產品用 recognize(crops)：
// 把寬度補齊到固定的級距、一次送多張進模型。補的 0 比官方版多，結果可能略有不同，
// 差異由 tools/eval 的一致性檢查量測（見 docs/design.md 4.4）。

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

// 批次辨識時，把補 0 後的寬度再往上對齊到固定的級距。級距少，DirectML 才不會因為每次輸入
// 大小不同而重新編譯；級距之間的間隔不大，才不會補太多 0。
int recognitionWidthBucket(int paddedWidth);

// 這個寬度的級距，一批放幾張。批次越大越快（送進模型的次數少），但一次的記憶體用量
// 和寬度成正比，所以用固定的寬度預算換算。
int recognitionBatchSize(int bucketWidth, int maxBatch);

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

    // crop：CV_8UC3 的裁切圖。一次一張，和官方版逐項一致。
    Recognition recognize(const cv::Mat& crop);

    // 一次辨識多張：寬度相近的會補齊到同一個級距併成一批。回傳的順序和 crops 相同。
    // 先分組再切批次，所以批次大小不影響補 0 的量；一批的張數由寬度預算決定
    // （窄的圖一次可以送很多張，寬的圖少一點），maxBatch 是上限。
    std::vector<Recognition> recognize(std::span<const cv::Mat> crops, int maxBatch = 64);

    const RecognitionModelConfig& config() const { return config_; }

private:
    RecognitionModelConfig config_;
    OnnxModel model_;
};

}  // namespace tmw::ocr
