// 漫畫的文字區塊偵測：comic-text-detector（見 docs/design.md 4.4「漫畫的文字偵測」、M2-02）。
//
// 它找的是「一個對話框裡的字」整塊，而不是一行一行。拿它來分段比只看行距準：
// 10 頁日文漫畫上，正確復原的區塊從 106 變成 112，被切開的從 13 變成 6
// （tools/eval/regroup_by_ctd.py）。它幾乎不框擬聲詞（M0-11：日文擬聲詞只框到 7%），
// 沒被框到的行照原本的方式分段。
//
// 移植自 tools/eval/comic_text_detector.py，後處理照 YOLOv5。
#pragma once

#include <filesystem>
#include <opencv2/core.hpp>
#include <span>
#include <vector>

#include "core/geometry.h"
#include "ocr/onnx_model.h"

namespace tmw::ocr {

struct ComicTextBlock {
    core::RectI rect;  // 原圖座標
    float score = 0.0f;
    int label = 0;  // 模型的兩個類別；目前沒有用到

    friend bool operator==(const ComicTextBlock&, const ComicTextBlock&) = default;
};

struct ComicTextOptions {
    float confidence = 0.4f;  // comic-text-detector 的預設值
    float nmsIou = 0.35f;
};

// 每個候選框有幾個數：中心 x、中心 y、寬、高、物件分數、2 個類別分數
inline constexpr int kComicTextCandidateStride = 7;

// YOLOv5 候選框的後處理：分數＝物件分數×類別分數，過門檻、換回原圖座標、NMS。
// candidates：模型的 blk 輸出攤平（候選框數 × 7），座標是 1024×1024 輸入上的。
// scale：原圖縮放到輸入時乘上的倍數。純計算，不需要模型，可以單獨測試。
std::vector<ComicTextBlock> decodeComicTextBlocks(std::span<const float> candidates, double scale,
                                                  core::SizeI imageSize,
                                                  const ComicTextOptions& options = {});

class ComicTextDetector {
public:
    // 模型的輸入固定是 1024×1024：原圖等比例縮放到長邊 1024，右邊和下面補 0
    static constexpr int kInputSize = 1024;

    // 建立失敗時丟出 std::runtime_error
    ComicTextDetector(const std::filesystem::path& modelFile, Device device,
                      const ComicTextOptions& options = {});

    // bgr：CV_8UC3。回傳原圖座標的文字區塊。
    // scale：送進模型前乘上的倍數。0 表示照參考實作把長邊縮放到 1024；
    // 指定的話會再壓到不超過 1024（模型的輸入是固定的）。
    std::vector<ComicTextBlock> detect(const cv::Mat& bgr, double scale = 0.0);

    Device device() const { return model_.device(); }

private:
    ComicTextOptions options_;
    OnnxModel model_;
};

}  // namespace tmw::ocr
