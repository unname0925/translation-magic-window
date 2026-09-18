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

}  // namespace
}  // namespace tmw::core
