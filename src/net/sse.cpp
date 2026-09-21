#include "net/sse.h"

#include <cstddef>

namespace tmw::net {
namespace {

constexpr std::string_view kDataPrefix = "data:";
constexpr std::string_view kDone = "[DONE]";

std::string_view trimLeadingSpace(std::string_view text) {
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    return text;
}

}  // namespace

std::vector<std::string> takeSseData(std::string& buffer) {
    std::vector<std::string> out;
    std::size_t consumed = 0;
    while (true) {
        const std::size_t end = buffer.find('\n', consumed);
        if (end == std::string::npos) {
            break;  // 這一行還沒收完
        }
        std::string_view line(buffer.data() + consumed, end - consumed);
        consumed = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == ':') {
            continue;  // 事件的分隔行或註解
        }
        if (line.compare(0, kDataPrefix.size(), kDataPrefix) != 0) {
            continue;  // event:、id: 之類的欄位用不到
        }
        const std::string_view payload = trimLeadingSpace(line.substr(kDataPrefix.size()));
        if (payload.empty() || payload == kDone) {
            continue;
        }
        out.emplace_back(payload);
    }
    buffer.erase(0, consumed);
    return out;
}

}  // namespace tmw::net
