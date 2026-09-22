// UTF-8 的解碼和編碼。
//
// 程式裡的文字一律是 UTF-8（OCR 的輸出、翻譯引擎的回應、設定檔），但這些來源都不保證合法，
// 所以這裡的解碼把壞掉的位元組當成 U+FFFD 跳過，不丟例外、也不會讀到字串外面。
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace tmw::core {

inline constexpr char32_t kReplacementCharacter = U'�';

// 取出 index 位置的字元並把 index 移到下一個字元。
// 壞掉的位元組（截斷、不合法的開頭、缺少後續位元組）會回傳 U+FFFD。
char32_t nextCodePoint(std::string_view utf8, std::size_t& index);

// 把一個字元接到字串後面
void appendCodePoint(std::string& out, char32_t c);

// 有幾個字（不是幾個位元組）
int characterCount(std::string_view utf8);

// 第 index 個字在字串中的位元組位置。index 大於等於字數時回傳字串長度。
std::size_t byteOffsetOfCharacter(std::string_view utf8, int index);

}  // namespace tmw::core
