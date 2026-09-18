#include "platform/win_error.h"

#include <windows.h>

#include <stdexcept>

#include "platform/text_encoding.h"

namespace tmw::platform {

std::string describeWin32Error(unsigned long code) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    if (length == 0 || buffer == nullptr) {
        return "unknown error";
    }
    std::wstring message(buffer, length);
    LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' ||
                                message.back() == L' ' || message.back() == L'.')) {
        message.pop_back();
    }
    return wideToUtf8(message);
}

void throwLastError(std::string_view what) {
    const DWORD code = GetLastError();
    std::string message(what);
    message += " (Win32 error " + std::to_string(code) + ": " + describeWin32Error(code) + ")";
    throw std::runtime_error(message);
}

}  // namespace tmw::platform
