// ocr 模組內部共用的 ONNX Runtime 設定（OnnxModel 和 OnnxSession 都用）。不要從模組外引用。
#pragma once

#include <onnxruntime_cxx_api.h>

#include <filesystem>
#include <string>

#include "ocr/onnx_model.h"

namespace tmw::ocr {

// 整個程式共用一個 Env
Ort::Env& onnxEnvironment();

// 建立推論工作階段。device 是 Auto 時先試 DirectML、失敗改用 CPU；
// 呼叫之後 device 會變成實際使用的裝置。失敗時丟出 Ort::Exception。
Ort::Session createOnnxSession(const std::filesystem::path& onnxFile, Device& device);

std::string onnxDisplayPath(const std::filesystem::path& path);

}  // namespace tmw::ocr
