// UTF-8 的解碼和編碼。OCR 的輸出和翻譯引擎的回應都不保證合法，壞掉的位元組不能讓程式當掉。
#include "core/utf8.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <ios>
#include <string>
#include <string_view>
#include <vector>

namespace tmw::core {
namespace {

std::vector<char32_t> decode(std::string_view text) {
    std::vector<char32_t> out;
    for (std::size_t i = 0; i < text.size();) {
        out.push_back(nextCodePoint(text, i));
    }
    return out;
}

TEST(Utf8Test, DecodesEveryLength) {
    EXPECT_EQ(decode("A"), (std::vector<char32_t>{U'A'}));
    EXPECT_EQ(decode("é"), (std::vector<char32_t>{0x00E9}));    // 兩位元組
    EXPECT_EQ(decode("あ"), (std::vector<char32_t>{0x3042}));   // 三位元組
    EXPECT_EQ(decode("🙂"), (std::vector<char32_t>{0x1F642}));  // 四位元組
}

TEST(Utf8Test, ReplacesBrokenBytes) {
    EXPECT_EQ(decode("\xE3\x81"), (std::vector<char32_t>{kReplacementCharacter})) << "被截斷";
    EXPECT_EQ(decode("\xFF"), (std::vector<char32_t>{kReplacementCharacter})) << "不合法的開頭";
    EXPECT_EQ(decode("\x80"), (std::vector<char32_t>{kReplacementCharacter}))
        << "沒有開頭的後續位元組";
}

TEST(Utf8Test, KeepsGoingAfterABrokenCharacter) {
    EXPECT_EQ(decode("\xE3\x81 A"), (std::vector<char32_t>{kReplacementCharacter, U' ', U'A'}));
}

TEST(Utf8Test, EncodesEveryLength) {
    std::string out;
    appendCodePoint(out, U'A');
    appendCodePoint(out, 0x00E9);
    appendCodePoint(out, 0x3042);
    appendCodePoint(out, 0x1F642);
    EXPECT_EQ(out, "Aéあ🙂");
}

TEST(Utf8Test, EncodeThenDecodeGivesTheSameCharacter) {
    for (const char32_t c : {U'A', char32_t{0x00E9}, char32_t{0x3042}, char32_t{0xAC00},
                             char32_t{0xFFFD}, char32_t{0x1F642}}) {
        std::string encoded;
        appendCodePoint(encoded, c);
        EXPECT_EQ(decode(encoded), (std::vector<char32_t>{c}))
            << "U+" << std::hex << static_cast<std::uint32_t>(c);
    }
}

}  // namespace
}  // namespace tmw::core
