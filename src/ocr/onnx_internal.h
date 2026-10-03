// ocr 模組內部共用的 ONNX Runtime 設定（OnnxModel 和 OnnxSession 都用）。不要從模組外引用。
#pragma once

#include <onnxruntime_cxx_api.h>

#include <filesystem>
#include <span>
#include <string>

#include "ocr/onnx_model.h"

namespace tmw::ocr {

// 整個程式共用一個 Env
Ort::Env& onnxEnvironment();

// 建立推論工作階段。device 是 Auto 時先試 DirectML、失敗改用 CPU；
// 呼叫之後 device 會變成實際使用的裝置。失敗時丟出 Ort::Exception。
// optimizeGraph：ONNX Runtime 自己的圖形最佳化。DirectML 上關掉它可以省下不少記憶體
// （manga-ocr：工作集 707 → 422 MB、私有 1235 → 1010 MB），速度幾乎不變（30.4 → 30.9 ms），
// 因為 DirectML 還是會做它自己的圖形融合。
// fixedDimensions：把模型裡可變的維度（依名稱）固定成這個大小。DirectML 只有形狀固定時才把整個
// 模型編譯成一張最佳化的圖；可變的維度在同一個工作階段裡只有第一次看到的大小跑得快
// （辨識模型：1 行×寬 384 是 3.6 ms 對 29 ms，docs/proposal-speed-and-web-manga.md）。
Ort::Session createOnnxSession(const std::filesystem::path& onnxFile, Device& device,
                               bool optimizeGraph = true,
                               std::span<const FixedDimension> fixedDimensions = {});

std::string onnxDisplayPath(const std::filesystem::path& path);

}  // namespace tmw::ocr
