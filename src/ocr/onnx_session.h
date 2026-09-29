// 有好幾個輸入、好幾個輸出的 ONNX 模型（見 docs/design.md 4.4）。
//
// OnnxModel 只接受一個輸入、一個輸出（PP-OCR 的模型都是），manga-ocr 的單步解碼有 10 個輸入
// （這一步的字、位置、前面累積的 K／V、圖像特徵的 K／V）和 5 個輸出，所以另外包一層。
// 裝置的選擇和 OnnxModel 相同：Auto 先試 DirectML，建立失敗就改用 CPU。
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include "ocr/onnx_model.h"

namespace tmw::ocr {

// 一個輸入。元素數可以是 0（例如第一步的 K／V 長度是 0），所以型別要明確標出來，
// 不能從「哪個 span 是空的」去猜。
struct NamedInput {
    const char* name = nullptr;
    std::span<const std::int64_t> shape;
    std::span<const float> floats;
    std::span<const std::int64_t> integers;
    bool integer = false;  // true：用 integers（int64）；false：用 floats

    static NamedInput ofFloats(const char* name, std::span<const std::int64_t> shape,
                               std::span<const float> data) {
        return {name, shape, data, {}, false};
    }
    static NamedInput ofIntegers(const char* name, std::span<const std::int64_t> shape,
                                 std::span<const std::int64_t> data) {
        return {name, shape, {}, data, true};
    }
};

class OnnxSession {
public:
    // 建立失敗時丟出 std::runtime_error
    OnnxSession(const std::filesystem::path& onnxFile, Device device);
    ~OnnxSession();

    OnnxSession(const OnnxSession&) = delete;
    OnnxSession& operator=(const OnnxSession&) = delete;

    Device device() const;

    // 回傳 outputNames 指定的輸出，順序和 outputNames 相同。輸出一律是 float。
    // 失敗時丟出 std::runtime_error。不是執行緒安全的。
    std::vector<Tensor> run(std::span<const NamedInput> inputs,
                            std::span<const char* const> outputNames);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tmw::ocr
