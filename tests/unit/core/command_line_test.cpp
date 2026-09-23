#include "core/command_line.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace tmw::core {
namespace {

CommandLineOptions parse(const std::vector<std::wstring>& args) {
    return parseCommandLine(args);
}

TEST(CommandLineTest, NoArgumentsUsesDefaults) {
    const CommandLineOptions options = parse({});
    EXPECT_FALSE(options.dataDirectory.has_value());
}

TEST(CommandLineTest, DataDirectoryTakesTheNextArgument) {
    const CommandLineOptions options = parse({L"--data-dir", L"C:\\temp\\測試 資料"});
    ASSERT_TRUE(options.dataDirectory.has_value());
    EXPECT_EQ(*options.dataDirectory, std::filesystem::path(L"C:\\temp\\測試 資料"));
}

TEST(CommandLineTest, DataDirectoryWithoutValueIsRejected) {
    EXPECT_THROW(parse({L"--data-dir"}), CommandLineError);
    EXPECT_THROW(parse({L"--data-dir", L""}), CommandLineError);
}

TEST(CommandLineTest, RepeatedDataDirectoryIsRejected) {
    EXPECT_THROW(parse({L"--data-dir", L"a", L"--data-dir", L"b"}), CommandLineError);
}

TEST(CommandLineTest, UnknownOptionIsRejectedWithItsNameInTheMessage) {
    try {
        parse({L"--dat-dir", L"a"});
        FAIL() << "expected CommandLineError";
    } catch (const CommandLineError& error) {
        EXPECT_NE(std::string(error.what()).find("--dat-dir"), std::string::npos) << error.what();
    }
}

TEST(CommandLineTest, UnknownOptionMessageIsUtf8) {
    try {
        parse({L"--語言"});
        FAIL() << "expected CommandLineError";
    } catch (const CommandLineError& error) {
        EXPECT_NE(std::string(error.what()).find("--語言"), std::string::npos) << error.what();
    }
}

TEST(CommandLineTest, OcrDeviceDefaultsToNothing) {
    EXPECT_FALSE(parse({}).ocrDevice.has_value()) << "沒指定就是 auto，由程式自己決定";
}

TEST(CommandLineTest, OcrDeviceTakesTheThreeDevices) {
    EXPECT_EQ(parse({L"--ocr-device", L"cpu"}).ocrDevice, std::optional<std::string>("cpu"));
    EXPECT_EQ(parse({L"--ocr-device", L"dml"}).ocrDevice, std::optional<std::string>("dml"));
    EXPECT_EQ(parse({L"--ocr-device", L"auto"}).ocrDevice, std::optional<std::string>("auto"));
}

TEST(CommandLineTest, OcrDeviceRejectsSomethingElse) {
    // 打錯字時要當場說清楚，而不是安靜地用 auto——測「沒有顯示卡的電腦」時
    // 以為在用 CPU 其實在用 GPU，整個測試就白做了
    try {
        parse({L"--ocr-device", L"gpu"});
        FAIL() << "expected CommandLineError";
    } catch (const CommandLineError& error) {
        EXPECT_NE(std::string(error.what()).find("gpu"), std::string::npos) << error.what();
    }
}

TEST(CommandLineTest, OcrDeviceNeedsAValue) {
    EXPECT_THROW(parse({L"--ocr-device"}), CommandLineError);
}

TEST(CommandLineTest, OcrDeviceCannotBeGivenTwice) {
    EXPECT_THROW(parse({L"--ocr-device", L"cpu", L"--ocr-device", L"dml"}), CommandLineError);
}

TEST(CommandLineTest, OcrDeviceAndDataDirectoryGoTogether) {
    const CommandLineOptions options = parse({L"--data-dir", L"C:\temp", L"--ocr-device", L"cpu"});
    EXPECT_EQ(options.dataDirectory, std::filesystem::path("C:\temp"));
    EXPECT_EQ(options.ocrDevice, std::optional<std::string>("cpu"));
}

}  // namespace
}  // namespace tmw::core
