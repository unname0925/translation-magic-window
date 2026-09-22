#pragma once

#include <filesystem>

namespace tmw::platform {

// 預設的資料夾：%LOCALAPPDATA%\TranslationMagicWindow（不存在時會建立）。
// 可以用命令列參數 --data-dir 改到別的地方。見 docs/design.md 4.11。
std::filesystem::path defaultDataDirectory();

// 擷取的 PNG 存放位置：<dataDirectory>\captures（不存在時會建立）。
std::filesystem::path capturesDirectory(const std::filesystem::path& dataDirectory);

// 執行檔所在的資料夾
std::filesystem::path executableDirectory();

// OCR 模型的資料夾：執行檔旁邊的 models 資料夾。開發時執行檔在 build 底下，
// 模型在倉庫根目錄，所以往上找幾層。都找不到時回傳空路徑。
std::filesystem::path findModelsDirectory();

}  // namespace tmw::platform
