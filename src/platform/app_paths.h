#pragma once

#include <filesystem>

namespace tmw::platform {

// 預設的資料夾：%LOCALAPPDATA%\TranslationMagicWindow（不存在時會建立）。
// 可以用命令列參數 --data-dir 改到別的地方。見 docs/design.md 4.10。
std::filesystem::path defaultDataDirectory();

// 擷取的 PNG 存放位置：<dataDirectory>\captures（不存在時會建立）。
std::filesystem::path capturesDirectory(const std::filesystem::path& dataDirectory);

}  // namespace tmw::platform
