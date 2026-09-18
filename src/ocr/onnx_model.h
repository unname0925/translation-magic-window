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
};

std::string_view deviceName(Device device);

struct Tensor {
    std::vector<std::int64_t> shape;
    std::vector<float> data;
};

// 一個 ONNX 模型的推論工作階段。只有一個輸入和一個輸出（PP-OCR 的偵測和辨識模型都是）。
// 建立失敗時丟出 std::runtime_error。不是執行緒安全的：同一個物件不要同時從多個執行緒呼叫。
class OnnxModel {
public:
    OnnxModel(const std::filesystem::path& onnxFile, Device device);
    ~OnnxModel();

    OnnxModel(const OnnxModel&) = delete;
    OnnxModel& operator=(const OnnxModel&) = delete;

    Device device() const;

    // 輸入一個 float 張量（例如 NCHW），回傳輸出張量。失敗時丟出 std::runtime_error。
    Tensor run(std::span<const float> input, std::span<const std::int64_t> shape);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tmw::ocr
