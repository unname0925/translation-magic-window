// M1-02：API 金鑰的 DPAPI 加密（見 docs/design.md 4.5）。
#include "platform/secret.h"

#include <gtest/gtest.h>

#include <string>

namespace tmw::platform {
namespace {

TEST(SecretTest, DecryptsWhatItEncrypted) {
    const std::string key = "sk-ant-api03-測試用的金鑰-0123456789";

    const std::string encrypted = encryptSecret(key);

    EXPECT_NE(encrypted, key);
    EXPECT_EQ(encrypted.find(key), std::string::npos) << "加密後的內容不可以看得到原文";
    EXPECT_EQ(decryptSecret(encrypted), key);
}

TEST(SecretTest, EncryptingTwiceGivesDifferentText) {
    // DPAPI 每次用不同的隨機值，所以同樣的金鑰不會產生一樣的字串
    const std::string key = "the-same-key";

    EXPECT_NE(encryptSecret(key), encryptSecret(key));
}

TEST(SecretTest, EmptyMeansNoKey) {
    EXPECT_EQ(encryptSecret(""), "");
    EXPECT_EQ(decryptSecret(""), "");
}

TEST(SecretTest, RefusesTextThatWasNotEncryptedHere) {
    EXPECT_FALSE(decryptSecret("這不是 base64").has_value());
    EXPECT_FALSE(decryptSecret("AQIDBAUGBwgJ").has_value());
}

TEST(SecretTest, RefusesModifiedText) {
    std::string encrypted = encryptSecret("some-api-key");
    ASSERT_GT(encrypted.size(), 20u);
    encrypted[encrypted.size() / 2] = encrypted[encrypted.size() / 2] == 'A' ? 'B' : 'A';

    EXPECT_FALSE(decryptSecret(encrypted).has_value());
}

TEST(SecretTest, HandlesTextWithNulBytes) {
    const std::string key("ab\0cd", 5);

    EXPECT_EQ(decryptSecret(encryptSecret(key)), key);
}

}  // namespace
}  // namespace tmw::platform
