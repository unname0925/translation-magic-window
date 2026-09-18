#pragma once

#include <filesystem>

namespace tmw::platform {

// %LOCALAPPDATA%\TranslationMagicWindow（不存在時會建立）。見 docs/design.md 4.10。
std::filesystem::path localDataDirectory();

// 手動擷取的 PNG 存放位置：<localDataDirectory>\captures（不存在時會建立）。
std::filesystem::path capturesDirectory();

}  // namespace tmw::platform
