#pragma once

#include <array>
#include <filesystem>
#include <memory>
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

// DirectML 上辨識用的固定形狀（一批幾行 × 補齊到多寬）。DirectML 只有形狀固定時才走快的路線，
// 可變的形狀在同一個工作階段裡只有第一次看到的大小快（1 行×寬 384：3.6 ms 對 29 ms）。
// 每個形狀要一個工作階段（每個約多 125 MB 記憶體），所以只開三個：用 179 個真實畫面的行寬
// 分佈模擬過（docs/proposal-speed-and-web-manga.md），這組平均 48 ms、p90 96 ms，現在約 100 ms，
// 而且沒有第一次遇到新形狀時的 850 ms 尖峰。行寬中位數 320、最寬 1222。
struct FixedRecognitionShape {
    int batch = 0;
    int width = 0;
};
inline constexpr std::array<FixedRecognitionShape, 3> kFixedRecognitionShapes{
    {{8, 384}, {4, 768}, {2, 1536}}};

// 這個寬度（補 0 之後）放進哪個固定形狀：放得下的最窄那個。都放不下時回傳 -1
int fixedRecognitionShapeFor(int paddedWidth);

// 這個寬度的級距，一批放幾張。批次越大越快（送進模型的次數少），但一次的記憶體用量
// 和寬度成正比，所以用固定的寬度預算換算。
int recognitionBatchSize(int bucketWidth, int maxBatch);

// CTC 貪婪解碼：每個時間點取機率最大的類別，合併連續重複的類別，再去掉 blank（索引 0）。
// probabilities 是 timeSteps × characters.size() 的機率。
Recognition ctcGreedyDecode(std::span<const float> probabilities, int timeSteps,
                            const std::vector<std::string>& characters);

// 同上，但輸入是模型已經挑好的每個時間點的最大值（tools/eval/rec_argmax.py 產生的
// inference_argmax.onnx）：maxima 是 timeSteps × 2，第 0 個是字元編號（float）、第 1 個是機率。
// 結果和 ctcGreedyDecode 完全相同，只是不必把整張機率表從顯示卡複製回來。
Recognition ctcDecodeMaxima(std::span<const float> maxima, int timeSteps,
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
    // DirectML 上，寬度放得進 kFixedRecognitionShapes 的行改用固定形狀的工作階段（見上）。
    std::vector<Recognition> recognize(std::span<const cv::Mat> crops, int maxBatch = 64);

    // 先建好固定形狀的工作階段並各跑一次，第一次辨識時才不會卡住。CPU 上不做任何事
    void warmUpFixedShapes();

    const RecognitionModelConfig& config() const { return config_; }

private:
    // 第 index 個固定形狀的工作階段，第一次用到時才建立
    OnnxModel& fixedModel(std::size_t index);
    // 一批固定形狀：items 是 crops 的索引，最多 shape.batch 個
    void recognizeFixed(std::span<const cv::Mat> crops, std::span<const std::size_t> items,
                        std::size_t shapeIndex, std::vector<Recognition>& results);

    std::filesystem::path modelFile_;
    // 固定形狀的工作階段用的模型：有 inference_argmax.onnx 就用它（輸出小很多），沒有就用原本的
    std::filesystem::path fixedModelFile_;
    bool fixedOutputsMaxima_ = false;
    RecognitionModelConfig config_;
    OnnxModel model_;
    std::array<std::unique_ptr<OnnxModel>, kFixedRecognitionShapes.size()> fixed_;
};

}  // namespace tmw::ocr
