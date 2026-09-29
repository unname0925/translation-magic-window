#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace tmw::ocr {

// 模型在哪個裝置上執行（見 docs/design.md 4.4）。
enum class Device {
    Cpu,
    DirectML,  // GPU（DirectX 12），第一張顯示卡
    Auto,      // 先試 DirectML，建立失敗（沒有相容的顯示卡、驅動有問題）就改用 CPU
};

std::string_view deviceName(Device device);

struct Tensor {
    std::vector<std::int64_t> shape;
    std::vector<float> data;
};

// 一個 ONNX 模型的推論工作階段。只有一個輸入；輸出只取一個
// （PP-OCR 的偵測和辨識模型都只有一個輸出，comic-text-detector 有三個，用 outputName 指定）。
// 建立失敗時丟出 std::runtime_error。不是執行緒安全的：同一個物件不要同時從多個執行緒呼叫。
class OnnxModel {
public:
    // outputName：模型有好幾個輸出時要取哪一個。空字串表示模型必須剛好只有一個輸出，
    // 多了就當成放錯模型（例如把 comic-text-detector 當成 PP-OCR 載入）。
    // optimizeGraph：ONNX Runtime 自己的圖形最佳化（見 onnx_internal.h 的量測）
    OnnxModel(const std::filesystem::path& onnxFile, Device device,
              std::string_view outputName = {}, bool optimizeGraph = true);
    ~OnnxModel();

    OnnxModel(const OnnxModel&) = delete;
    OnnxModel& operator=(const OnnxModel&) = delete;

    // 實際使用的裝置（Auto 會解析成 Cpu 或 DirectML）
    Device device() const;

    // 輸入一個 float 張量（例如 NCHW），回傳輸出張量。失敗時丟出 std::runtime_error。
    Tensor run(std::span<const float> input, std::span<const std::int64_t> shape);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tmw::ocr
