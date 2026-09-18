#pragma once

#include <string>
#include <string_view>

namespace tmw::platform {

// UTF-8 與 UTF-16 互轉。core 一律使用 UTF-8，只有呼叫 Win32 API 時才轉成 UTF-16。
// 無效的位元組序列（或落單的 surrogate）會被替換成 U+FFFD，不會讓轉換失敗。
std::wstring utf8ToWide(std::string_view utf8);
std::string wideToUtf8(std::wstring_view wide);

}  // namespace tmw::platform
