// UT-07（後半）：已知詞彙能正確轉成台灣用語（例如「软件」轉成「軟體」）。
#include "core/opencc_converter.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>

namespace tmw::core {
namespace {

// 字典檔的位置由 CMake 填入（見 cmake/Opencc.cmake）
std::filesystem::path dataDirectory() {
    return std::filesystem::path(TMW_OPENCC_DATA_DIR);
}

class OpenccConverterTest : public ::testing::Test {
protected:
    OpenccConverter converter_{OpenccConverter::defaultConfig(dataDirectory())};
};

TEST_F(OpenccConverterTest, ConvertsSimplifiedToTraditional) {
    EXPECT_EQ(converter_.convert("这是什么"), "這是什麼");
}

TEST_F(OpenccConverterTest, UsesTaiwanWording) {
    // s2twp 不只換字，還會換用詞；LLM 和 Google 常常回中國用語
    EXPECT_EQ(converter_.convert("软件"), "軟體");
    EXPECT_EQ(converter_.convert("服务器"), "伺服器");
    EXPECT_EQ(converter_.convert("内存"), "記憶體");
}

TEST_F(OpenccConverterTest, LeavesTraditionalTextAlone) {
    EXPECT_EQ(converter_.convert("這是什麼"), "這是什麼");
    EXPECT_EQ(converter_.convert("軟體"), "軟體");
}

TEST_F(OpenccConverterTest, LeavesNonChineseTextAlone) {
    EXPECT_EQ(converter_.convert("Hello, world!"), "Hello, world!");
    EXPECT_EQ(converter_.convert(""), "");
    EXPECT_EQ(converter_.convert("110/110"), "110/110");
}

TEST_F(OpenccConverterTest, KeepsRubyMarkers) {
    // ルビ 的標記不能被轉換弄壞，否則對齊檢查會失敗
    EXPECT_EQ(converter_.convert("{认真|玩真的}"), "{認真|玩真的}");
}

TEST(OpenccConverterErrorTest, ReportsAMissingConfigFile) {
    EXPECT_THROW(OpenccConverter(dataDirectory() / "沒有這個檔案.json"), std::runtime_error);
}

TEST(NullTextConverterTest, ReturnsTheInput) {
    const NullTextConverter converter;
    EXPECT_EQ(converter.convert("这是什么"), "这是什么");
}

}  // namespace
}  // namespace tmw::core
