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

// 選模型之前先決定 Auto 要怎麼跑（M2-11）。
// DirectML 用的是第一張顯示卡；它是軟體轉譯器（Microsoft Basic Render Driver，沒有 GPU 的
// 電腦、遠端桌面）時，DirectML 可能照樣建得起來，實際卻在 CPU 上模擬 GPU，非常慢，
// 而且模型會被選成 GPU 用的 medium（CPU 上 1.1 秒，small 只要 0.3 秒）。
// - requested 不是 Auto：原樣回傳，使用者指定的就照辦。
// - Auto、第一張顯示卡是硬體：維持 Auto（建立失敗時仍然會退回 CPU）。
// - Auto、沒有硬體顯示卡：Cpu。
Device resolveDevice(Device requested, bool firstAdapterIsHardware);

// DirectML 會用的第一張顯示卡是不是硬體（DXGI 的 DXGI_ADAPTER_FLAG_SOFTWARE）
bool firstAdapterIsHardware();

// 找得到 DirectML.dll 嗎。程式預設不附 DirectML.dll（它是 Microsoft 的專有授權，本程式是
// GPL-3.0）， 用 Windows 內建在 System32 的那一份；很舊的 Windows 10 沒有它。ONNX Runtime
// 是延遲載入 DirectML 的， 沒有它時一用 DirectML 就會當掉，所以要先檢查。
bool directMLAvailable();

// 實際載入的 DirectML.dll 的完整路徑（記錄檔和除錯傾印用，看得出用的是系統內建的還是程式附的）。
// 還沒載入或找不到時是空的。
std::filesystem::path loadedDirectMLPath();

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
