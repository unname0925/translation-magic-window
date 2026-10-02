#include "core/hotkey.h"

#include <cctype>
#include <cstddef>
#include <vector>

namespace tmw::core {
namespace {

std::string trimmedUpper(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    std::string out;
    for (std::size_t i = begin; i < end; ++i) {
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
    }
    return out;
}

bool isFunctionKey(const std::string& key) {
    if (key.size() < 2 || key.size() > 3 || key[0] != 'F') {
        return false;
    }
    for (std::size_t i = 1; i < key.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(key[i])) == 0) {
            return false;
        }
    }
    const int number = std::stoi(key.substr(1));
    return number >= 1 && number <= 24;
}

bool isPlainKey(const std::string& key) {
    return key.size() == 1 && std::isalnum(static_cast<unsigned char>(key[0])) != 0;
}

}  // namespace

std::optional<Hotkey> parseHotkey(std::string_view text) {
    Hotkey hotkey;
    std::size_t at = 0;
    while (true) {
        const std::size_t plus = text.find('+', at);
        const std::string part =
            trimmedUpper(text.substr(at, plus == std::string_view::npos ? text.npos : plus - at));
        const bool last = plus == std::string_view::npos;
        if (part == "CTRL" || part == "CONTROL") {
            hotkey.ctrl = true;
        } else if (part == "ALT") {
            hotkey.alt = true;
        } else if (part == "SHIFT") {
            hotkey.shift = true;
        } else if (part == "WIN" || part == "META") {
            hotkey.win = true;
        } else if (last && (isPlainKey(part) || isFunctionKey(part))) {
            hotkey.key = part;
        } else {
            return std::nullopt;  // 看不懂的部分，或按鍵不在最後
        }
        if (last) {
            break;
        }
        at = plus + 1;
    }
    if (hotkey.key.empty()) {
        return std::nullopt;  // 只有修飾鍵
    }
    const bool modified = hotkey.ctrl || hotkey.alt || hotkey.shift || hotkey.win;
    if (!modified && !isFunctionKey(hotkey.key)) {
        return std::nullopt;
    }
    if (!hotkey.ctrl && !hotkey.alt && !hotkey.win && hotkey.shift && !isFunctionKey(hotkey.key)) {
        return std::nullopt;  // Shift+T 就是大寫的 T，打字時會被吃掉
    }
    return hotkey;
}

std::string formatHotkey(const Hotkey& hotkey) {
    std::string out;
    const auto add = [&out](const char* part) {
        out += out.empty() ? "" : "+";
        out += part;
    };
    if (hotkey.ctrl) {
        add("Ctrl");
    }
    if (hotkey.alt) {
        add("Alt");
    }
    if (hotkey.shift) {
        add("Shift");
    }
    if (hotkey.win) {
        add("Win");
    }
    add(hotkey.key.c_str());
    return out;
}

std::string hotkeyProblem(std::span<const std::string> names, std::span<const std::string> texts) {
    std::vector<std::pair<std::string, Hotkey>> used;
    for (std::size_t i = 0; i < texts.size() && i < names.size(); ++i) {
        if (texts[i].empty()) {
            continue;  // 不使用這個功能
        }
        const std::optional<Hotkey> hotkey = parseHotkey(texts[i]);
        if (!hotkey) {
            return "「" + names[i] + "」的快捷鍵「" + texts[i] +
                   "」不能用：要有 Ctrl、Alt 或 Win，再加一個英文字母、數字或 F1～F24";
        }
        for (const auto& [otherName, other] : used) {
            if (other == *hotkey) {
                return "「" + names[i] + "」和「" + otherName + "」都是 " + formatHotkey(*hotkey);
            }
        }
        used.emplace_back(names[i], *hotkey);
    }
    return {};
}

}  // namespace tmw::core
