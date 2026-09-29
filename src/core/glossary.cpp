#include "core/glossary.h"

#include <cstddef>

namespace tmw::core {
namespace {

std::string_view trim(std::string_view text) {
    // 半形和全形空白都去掉（中文輸入法很容易打出全形空白）
    const auto isSpace = [](std::string_view s, std::size_t at) -> std::size_t {
        if (at < s.size() && (s[at] == ' ' || s[at] == '\t' || s[at] == '\r')) {
            return 1;
        }
        if (s.substr(at).starts_with("\xE3\x80\x80")) {  // U+3000
            return 3;
        }
        return 0;
    };
    while (const std::size_t n = isSpace(text, 0)) {
        text.remove_prefix(n);
    }
    while (!text.empty()) {
        if (text.back() == ' ' || text.back() == '\t' || text.back() == '\r') {
            text.remove_suffix(1);
        } else if (text.ends_with("\xE3\x80\x80")) {
            text.remove_suffix(3);
        } else {
            break;
        }
    }
    return text;
}

// 第一個「=」或「＝」的位置和長度
std::pair<std::size_t, std::size_t> findSeparator(std::string_view line) {
    const std::size_t half = line.find('=');
    const std::size_t full = line.find("\xEF\xBC\x9D");  // U+FF1D ＝
    if (full != std::string_view::npos && (half == std::string_view::npos || full < half)) {
        return {full, 3};
    }
    return {half, 1};
}

}  // namespace

GlossaryLoad parseGlossary(std::string_view text) {
    if (text.starts_with("\xEF\xBB\xBF")) {  // 記事本存的 UTF-8 有 BOM
        text.remove_prefix(3);
    }
    GlossaryLoad load;
    int number = 0;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view raw = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        ++number;

        const std::string_view line = trim(raw);
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        const auto [at, width] = findSeparator(line);
        if (at == std::string_view::npos) {
            load.problems.push_back("第 " + std::to_string(number) + " 行沒有「=」");
            continue;
        }
        const std::string_view source = trim(line.substr(0, at));
        const std::string_view target = trim(line.substr(at + width));
        if (source.empty() || target.empty()) {
            load.problems.push_back("第 " + std::to_string(number) + " 行的原文或譯文是空的");
            continue;
        }
        load.entries[std::string(source)] = std::string(target);
    }
    return load;
}

Glossary glossaryFor(std::span<const std::string> segments, const Glossary& glossary) {
    Glossary used;
    for (const auto& [source, target] : glossary) {
        for (const std::string& segment : segments) {
            if (segment.find(source) != std::string::npos) {
                used.emplace(source, target);
                break;
            }
        }
    }
    return used;
}

std::string glossaryFingerprint(std::string_view text, const Glossary& glossary) {
    std::string out;
    for (const auto& [source, target] : glossary) {
        if (text.find(source) != std::string_view::npos) {
            out += source;
            out += '=';
            out += target;
            out += '\n';
        }
    }
    return out;
}

std::string_view glossaryTemplate() {
    return "# 專有名詞表：一行一個詞，寫成「原文=譯文」。存檔後下一次翻譯就會生效。\n"
           "# 只有 LLM 引擎會照表翻譯（Google 翻譯不支援）。\n"
           "# 以 # 開頭的行是註解。例如：\n"
           "# 悠真=悠真\n"
           "# エクスカリバー=王者之劍\n";
}

}  // namespace tmw::core
