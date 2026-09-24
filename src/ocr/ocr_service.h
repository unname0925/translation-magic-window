// 把 ocr 模組接到 core 的處理管線上（core::IOcrService 的實作）。
//
// core 不認得 OpenCV，也不該認得模型；這一層負責影像格式轉換和座標整理。
#pragma once

#include <filesystem>
#include <memory>
#include <stop_token>
#include <vector>

#include "core/image.h"
#include "core/pipeline.h"
#include "core/text_layout.h"
#include "ocr/model_choice.h"
#include "ocr/ocr_pipeline.h"

namespace tmw::ocr {

class OcrService final : public core::IOcrService {
public:
    // 依語言和裝置挑模型（M0-11）。device 是 Device::Auto 時會解析成實際可用的裝置。
    // 只載入一個辨識模型，語言不會自動判斷。
    OcrService(const std::filesystem::path& modelsDirectory, TextLanguage language, Device device,
               const OcrOptions& options = {});

    // 正式程式用這個：主模型（日文／英文）和韓文模型都載入，語言自動判斷
    // （design.md 4.4「語言判斷」）。多一個模型的代價是載入時間和記憶體，
    // 換來的是韓文真的讀得出來——用日文模型讀韓文只會得到一堆空字串。
    OcrService(const std::filesystem::path& modelsDirectory, Device device,
               const OcrOptions& options = {});

    core::OcrResult recognize(const core::ImageBgra& frame, core::Language script,
                              std::stop_token cancel) override;

    Device device() const { return pipeline_.device(); }
    const OcrTimings& lastTimings() const { return lastTimings_; }

private:
    OcrPipeline pipeline_;
    OcrTimings lastTimings_;
};

// BGRA（每列緊密排列）轉成 OCR 要的 BGR。空畫面回傳空矩陣。
cv::Mat toBgr(const core::ImageBgra& frame);

// 文字框的四個點 → 外接矩形和橫直排。
// 直排的判定和 design.md 4.4 一致：高度大於寬度的 1.5 倍。
core::OcrLine toOcrLine(const TextLine& line);

}  // namespace tmw::ocr
