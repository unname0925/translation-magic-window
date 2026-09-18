#include "platform/text_encoding.h"

#include <gtest/gtest.h>

#include <string>

namespace tmw::platform {
namespace {

TEST(TextEncodingTest, EmptyStrings) {
    EXPECT_EQ(utf8ToWide(""), L"");
    EXPECT_EQ(wideToUtf8(L""), "");
}

TEST(TextEncodingTest, KnownCodePoints) {
    // 「中」= U+4E2D = E4 B8 AD
    EXPECT_EQ(utf8ToWide("\xE4\xB8\xAD"), std::wstring(1, static_cast<wchar_t>(0x4E2D)));
    EXPECT_EQ(wideToUtf8(std::wstring(1, static_cast<wchar_t>(0x4E2D))), "\xE4\xB8\xAD");
}

TEST(TextEncodingTest, RoundTripsAllSourceLanguagesAndEmoji) {
    // 繁中、日文（假名＋漢字）、韓文、英文，以及需要 surrogate pair 的 emoji
    const std::string original = "喂，等一下！おい、待てよ！안녕하세요 Hello 😀";
    const std::wstring wide = utf8ToWide(original);
    EXPECT_EQ(wide, L"喂，等一下！おい、待てよ！안녕하세요 Hello 😀");
    EXPECT_EQ(wideToUtf8(wide), original);
}

TEST(TextEncodingTest, EmbeddedNullIsPreserved) {
    const std::string original("a\0b", 3);
    EXPECT_EQ(wideToUtf8(utf8ToWide(original)), original);
}

TEST(TextEncodingTest, InvalidUtf8BecomesReplacementCharacter) {
    EXPECT_EQ(utf8ToWide("\xFF"), std::wstring(1, static_cast<wchar_t>(0xFFFD)));
}

TEST(TextEncodingTest, LoneSurrogateBecomesReplacementCharacter) {
    const std::wstring loneSurrogate(1, static_cast<wchar_t>(0xD800));
    EXPECT_EQ(wideToUtf8(loneSurrogate), "\xEF\xBF\xBD");  // U+FFFD
}

}  // namespace
}  // namespace tmw::platform
