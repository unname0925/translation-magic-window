// 當機傾印（M1-14）。傾印檔真的寫出來了嗎？打得開嗎？裡面有沒有當時的例外？
#include "platform/crash_dump.h"

#include <windows.h>

#include <dbghelp.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace tmw::platform {
namespace {

// SEH（__try）不能和需要解構的 C++ 物件放在同一個函式裡，
// 所以引數和結果都放在這裡，__try 的那個函式裡只剩函式指標
std::filesystem::path g_directoryForException;
std::optional<std::filesystem::path> g_fromException;

void dumpFromException(_EXCEPTION_POINTERS* exception) {
    g_fromException = writeCrashDump(exception, g_directoryForException);
}

void raiseAndDump(void (*onException)(_EXCEPTION_POINTERS*)) {
    __try {
        RaiseException(0xE0000001, 0, 0, nullptr);
    } __except (onException(GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER) {
    }
}

std::vector<char> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(file),
                             std::istreambuf_iterator<char>());
}

// 從傾印檔裡讀出當時的例外代碼。讀不到（沒有例外資訊）時回傳 nullopt。
std::optional<std::uint32_t> exceptionCodeIn(std::vector<char>& dump) {
    MINIDUMP_EXCEPTION_STREAM* stream = nullptr;
    ULONG size = 0;
    if (MiniDumpReadDumpStream(dump.data(), ExceptionStream, nullptr,
                               reinterpret_cast<void**>(&stream), &size) == FALSE ||
        stream == nullptr) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(stream->ExceptionRecord.ExceptionCode);
}

class CrashDumpTest : public ::testing::Test {
protected:
    void SetUp() override {
        directory_ = std::filesystem::temp_directory_path() /
                     ("tmw-dumps-" + std::to_string(GetCurrentProcessId()) + "-" +
                      ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(directory_);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    std::filesystem::path directory_;
};

TEST_F(CrashDumpTest, DumpsGoNextToTheLogs) {
    EXPECT_EQ(dumpsDirectory("C:\\data"), std::filesystem::path("C:\\data\\dumps"));
}

TEST_F(CrashDumpTest, TheFileNameSaysWhen) {
    EXPECT_EQ(dumpFileName(L"crash", 2026, 9, 22, 13, 45, 1), L"crash-20260922-134501.dmp");
    // 個位數的月、日、時、分、秒都補 0，檔名排序就是時間順序
    EXPECT_EQ(dumpFileName(L"crash", 2026, 1, 2, 3, 4, 5), L"crash-20260102-030405.dmp");
}

TEST_F(CrashDumpTest, WritesSomethingADebuggerCanOpen) {
    const std::optional<std::filesystem::path> path = writeCrashDump(nullptr, directory_);
    ASSERT_TRUE(path.has_value()) << "寫不出傾印檔";
    ASSERT_TRUE(std::filesystem::exists(*path));

    std::vector<char> dump = readFile(*path);
    ASSERT_GE(dump.size(), 4096u) << "傾印檔小得不像話：" << dump.size() << " 位元組";
    EXPECT_EQ(std::string(dump.data(), 4), "MDMP") << "不是 minidump 的格式";

    // 沒有例外的傾印（例如使用者自己按下傾印快捷鍵）就不該有例外資訊
    EXPECT_FALSE(exceptionCodeIn(dump).has_value());
}

TEST_F(CrashDumpTest, KeepsTheExceptionThatCausedIt) {
    g_fromException.reset();
    g_directoryForException = directory_;
    raiseAndDump(dumpFromException);
    ASSERT_TRUE(g_fromException.has_value()) << "例外發生時沒有寫出傾印檔";

    std::vector<char> dump = readFile(*g_fromException);
    ASSERT_GE(dump.size(), 4096u);
    // 0xE0000001 就是上面 RaiseException 丟的那個；查當機原因靠的就是它
    EXPECT_EQ(exceptionCodeIn(dump), std::optional<std::uint32_t>(0xE0000001u));
}

TEST_F(CrashDumpTest, TheFolderIsCreatedIfItIsNotThere) {
    ASSERT_FALSE(std::filesystem::exists(directory_));
    EXPECT_TRUE(writeCrashDump(nullptr, directory_).has_value());
    EXPECT_TRUE(std::filesystem::is_directory(directory_));
}

TEST_F(CrashDumpTest, InstallingAndRemovingLeavesTheHandlerAsItWas) {
    // SetUnhandledExceptionFilter 會回傳「之前是誰」，用它看得出裝上去了沒
    LPTOP_LEVEL_EXCEPTION_FILTER before = SetUnhandledExceptionFilter(nullptr);
    SetUnhandledExceptionFilter(before);

    ASSERT_TRUE(installCrashHandler(directory_));
    LPTOP_LEVEL_EXCEPTION_FILTER installed = SetUnhandledExceptionFilter(nullptr);
    SetUnhandledExceptionFilter(installed);
    EXPECT_NE(installed, before) << "處理常式沒有裝上去";
    EXPECT_TRUE(std::filesystem::is_directory(directory_)) << "資料夾要先建好，當機時才不必建";

    removeCrashHandler();
    LPTOP_LEVEL_EXCEPTION_FILTER after = SetUnhandledExceptionFilter(nullptr);
    SetUnhandledExceptionFilter(after);
    EXPECT_EQ(after, before) << "拿掉之後要換回原本的";
}

TEST_F(CrashDumpTest, RefusesAPathThatWouldNotFitInTheHandlersBuffer) {
    // 當機的時候不能配置記憶體，緩衝區是固定大小的；放不下時要說不，不能寫壞記憶體
    std::filesystem::path tooLong = directory_;
    for (int i = 0; i < 400; ++i) {
        tooLong /= std::wstring(100, L'a');
    }
    EXPECT_FALSE(installCrashHandler(tooLong));
    removeCrashHandler();
}

}  // namespace
}  // namespace tmw::platform
