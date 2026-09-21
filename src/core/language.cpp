#include "core/language.h"

#include <cstddef>

#include "core/utf8.h"

namespace tmw::core {
namespace {

bool isKana(char32_t c) {
    return (c >= 0x3040 && c <= 0x30FF) ||  // 平假名、片假名
           (c >= 0x31F0 && c <= 0x31FF) ||  // 片假名語音擴充
           (c >= 0xFF66 && c <= 0xFF9D);    // 半形片假名
}

bool isHan(char32_t c) {
    return (c >= 0x3400 && c <= 0x4DBF) ||  // 擴充 A
           (c >= 0x4E00 && c <= 0x9FFF) ||  // 基本區
           (c >= 0xF900 && c <= 0xFAFF) ||  // 相容字
           (c >= 0x20000 && c <= 0x2FA1F);  // 擴充 B 以後
}

bool isHangul(char32_t c) {
    return (c >= 0xAC00 && c <= 0xD7A3) ||  // 音節
           (c >= 0x1100 && c <= 0x11FF) ||  // 字母
           (c >= 0x3130 && c <= 0x318F) ||  // 相容字母
           (c >= 0xA960 && c <= 0xA97F) || (c >= 0xD7B0 && c <= 0xD7FF);
}

bool isLatin(char32_t c) {
    return (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') ||
           (c >= 0x00C0 && c <= 0x024F) ||                                // 拉丁字母補充、擴充
           (c >= 0xFF21 && c <= 0xFF3A) || (c >= 0xFF41 && c <= 0xFF5A);  // 全形英文
}

}  // namespace

std::string languageCode(Language language) {
    switch (language) {
        case Language::Japanese:
            return "ja";
        case Language::English:
            return "en";
        case Language::Korean:
            return "ko";
        case Language::Unknown:
            break;
    }
    return "";
}

ScriptCounts countScripts(std::string_view utf8) {
    ScriptCounts counts;
    for (std::size_t i = 0; i < utf8.size();) {
        const char32_t c = nextCodePoint(utf8, i);
        if (isKana(c)) {
            ++counts.kana;
        } else if (isHangul(c)) {
            ++counts.hangul;
        } else if (isHan(c)) {
            ++counts.han;
        } else if (isLatin(c)) {
            ++counts.latin;
        } else {
            ++counts.other;
        }
    }
    return counts;
}

Language detectLanguage(std::string_view utf8) {
    const ScriptCounts counts = countScripts(utf8);
    // 韓文和日文都可能夾雜漢字和英文，所以先看只屬於某一種語言的字母
    if (counts.hangul > 0 && counts.hangul >= counts.kana) {
        return Language::Korean;
    }
    if (counts.kana > 0) {
        return Language::Japanese;
    }
    if (counts.han > 0) {
        // 來源語言不含中文，純漢字當作日文（design.md 4.4）
        return Language::Japanese;
    }
    if (counts.latin > 0) {
        return Language::English;
    }
    return Language::Unknown;
}

}  // namespace tmw::core
