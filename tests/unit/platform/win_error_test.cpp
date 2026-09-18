#include "platform/win_error.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace tmw::platform {
namespace {

TEST(WinErrorTest, DescribesKnownErrorWithoutTrailingNewline) {
    const std::string text = describeWin32Error(ERROR_FILE_NOT_FOUND);
    ASSERT_FALSE(text.empty());
    EXPECT_NE(text.back(), '\n');
    EXPECT_NE(text.back(), '\r');
}

TEST(WinErrorTest, ThrowLastErrorIncludesContextAndCode) {
    SetLastError(ERROR_ACCESS_DENIED);
    std::string message;
    try {
        throwLastError("opening the thing");
    } catch (const std::runtime_error& error) {
        message = error.what();
    }
    EXPECT_NE(message.find("opening the thing"), std::string::npos) << message;
    EXPECT_NE(message.find("Win32 error 5"), std::string::npos) << message;
}

}  // namespace
}  // namespace tmw::platform
