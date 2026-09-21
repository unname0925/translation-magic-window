#include "core/utf8.h"

namespace tmw::core {

char32_t nextCodePoint(std::string_view utf8, std::size_t& index) {
    const auto byte = static_cast<unsigned char>(utf8[index]);
    const auto tail = [&](std::size_t count) -> char32_t {
        if (index + count >= utf8.size()) {
            index = utf8.size();
            return kReplacementCharacter;
        }
        char32_t value = byte & (0x7Fu >> (count + 1));
        for (std::size_t i = 1; i <= count; ++i) {
            const auto next = static_cast<unsigned char>(utf8[index + i]);
            if ((next & 0xC0u) != 0x80u) {
                // 後續位元組不合法：只跳過已經讀掉的部分，下一個字元從這裡重新開始
                index += i;
                return kReplacementCharacter;
            }
            value = (value << 6) | (next & 0x3Fu);
        }
        index += count + 1;
        return value;
    };

    if (byte < 0x80u) {
        ++index;
        return byte;
    }
    if ((byte & 0xE0u) == 0xC0u) {
        return tail(1);
    }
    if ((byte & 0xF0u) == 0xE0u) {
        return tail(2);
    }
    if ((byte & 0xF8u) == 0xF0u) {
        return tail(3);
    }
    ++index;
    return kReplacementCharacter;
}

void appendCodePoint(std::string& out, char32_t c) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0u | (c >> 6)));
        out.push_back(static_cast<char>(0x80u | (c & 0x3Fu)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0u | (c >> 12)));
        out.push_back(static_cast<char>(0x80u | ((c >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (c & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (c >> 18)));
        out.push_back(static_cast<char>(0x80u | ((c >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((c >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (c & 0x3Fu)));
    }
}

}  // namespace tmw::core
