#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "core/image.h"

namespace tmw::platform {

// 用 Windows Imaging Component 讀寫 PNG。呼叫前，目前執行緒必須已經初始化 COM。
// 失敗時丟出 std::runtime_error。

void savePng(const core::ImageBgra& image, const std::filesystem::path& path);
// 編成 PNG 放在記憶體裡（網頁漫畫的背景修補小圖）
std::vector<std::uint8_t> encodePng(const core::ImageBgra& image);

// 讀取任何 WIC 支援的影像（PNG、JPG、BMP…），轉成 BGRA。
core::ImageBgra loadImage(const std::filesystem::path& path);

}  // namespace tmw::platform
