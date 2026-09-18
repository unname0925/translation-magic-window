#pragma once

#include <string>
#include <string_view>

namespace tmw::platform {

// Win32 錯誤碼的系統說明文字（UTF-8，已去掉結尾的換行）。
std::string describeWin32Error(unsigned long code);

// 丟出 std::runtime_error，訊息包含 what 和 GetLastError() 的錯誤碼與說明。
[[noreturn]] void throwLastError(std::string_view what);

}  // namespace tmw::platform
