// LaMa 背景修補（M4-01，design.md 4.8）。
//
// 模型固定吃 512×512：在要抹掉的範圍周圍多取一圈當參考，縮放成 512×512 送進去，
// 結果縮回原大小，只取要抹掉的那一塊。
//
// 只用顯示卡（DirectML）：CPU 上每塊要 1.2～1.5 秒，照設計「沒有 GPU 時用純色填補」。
// 模型是 tools/eval/lama_for_directml.py 改寫過的版本（原本的 5 維 MatMul DirectML 不支援）。
// 第一次需要修補時才載入（多占約 340 MB 記憶體），用不到的人不必付。
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "core/geometry.h"
#include "core/image.h"
#include "core/inpainter.h"

namespace tmw::ocr {

class OnnxSession;

// 送進模型的那一塊：rect 往外多取 margin。放得下就取正方形（不變形），放不下就取裁到畫面內的長方形
core::RectI lamaCrop(core::SizeI frame, const core::RectI& rect, int margin = 64);

class LamaInpainter final : public core::IInpainter {
public:
    static constexpr int kInputSize = 512;

    explicit LamaInpainter(std::filesystem::path model);
    ~LamaInpainter() override;

    LamaInpainter(const LamaInpainter&) = delete;
    LamaInpainter& operator=(const LamaInpainter&) = delete;

    // 載入失敗（沒有顯示卡、檔案壞了）之後一律回傳 nullopt，不再重試
    std::optional<core::ImageBgra> inpaint(const core::ImageBgra& frame,
                                           const core::RectI& rect) override;

    // 載入失敗的原因；還沒載入或載入成功時是空的
    const std::string& problem() const { return problem_; }

    // models/lama/lama_fp32_dml.onnx
    static std::filesystem::path modelPath(const std::filesystem::path& modelsDirectory);

private:
    bool ensureLoaded();

    std::filesystem::path model_;
    std::unique_ptr<OnnxSession> session_;
    bool failed_ = false;
    std::string problem_;
};

}  // namespace tmw::ocr
