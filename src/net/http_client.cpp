#include "net/http_client.h"

#include <array>
#include <cstddef>

namespace tmw::net {
namespace {

bool isUnreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '_' || c == '.' || c == '~';
}

}  // namespace

std::string percentEncode(std::string_view text) {
    static constexpr std::array<char, 16> kDigits{'0', '1', '2', '3', '4', '5', '6', '7',
                                                  '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    std::string out;
    out.reserve(text.size());
    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        if (isUnreserved(c)) {
            out.push_back(raw);
        } else {
            out.push_back('%');
            out.push_back(kDigits[c >> 4]);
            out.push_back(kDigits[c & 0x0F]);
        }
    }
    return out;
}

std::string buildUrl(std::string_view base,
                     const std::vector<std::pair<std::string, std::string>>& parameters) {
    std::string out(base);
    bool first = out.find('?') == std::string::npos;
    for (const auto& [name, value] : parameters) {
        out += first ? '?' : '&';
        first = false;
        out += percentEncode(name);
        out += '=';
        out += percentEncode(value);
    }
    return out;
}

}  // namespace tmw::net
