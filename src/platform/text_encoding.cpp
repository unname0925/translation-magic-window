#include "platform/text_encoding.h"

#include <windows.h>

#include <limits>
#include <stdexcept>

namespace tmw::platform {
namespace {

int checkedLength(size_t size) {
    if (size > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("string is too long to convert");
    }
    return static_cast<int>(size);
}

}  // namespace

std::wstring utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int sourceLength = checkedLength(utf8.size());
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), sourceLength, nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), sourceLength, result.data(), length);
    return result;
}

std::string wideToUtf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int sourceLength = checkedLength(wide.size());
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), sourceLength, nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), sourceLength, result.data(), length, nullptr,
                        nullptr);
    return result;
}

}  // namespace tmw::platform
