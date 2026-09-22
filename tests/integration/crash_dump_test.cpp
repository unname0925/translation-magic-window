// M1-14 的驗收條件「故意觸發當機會產生 dump」。
//
// 單元測試只能證明「叫得動 MiniDumpWriteDump」；真正要確認的是**另一個程序真的當掉時**
// 處理常式會被叫到、傾印檔會出現。所以這裡啟動 tmw_crash_helper.exe，讓它寫入空指標。
#include "platform/crash_dump.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace tmw {
namespace {

std::filesystem::path crashHelperPath() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path() / L"tmw_crash_helper.exe";
}

// 執行 tmw_crash_helper.exe，回傳它的結束代碼
DWORD runCrashHelper(const std::filesystem::path& dumpDirectory) {
    std::wstring commandLine =
        L"\"" + crashHelperPath().wstring() + L"\" \"" + dumpDirectory.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &startup, &process) == FALSE) {
        return 0;
    }
    // 寫傾印檔要一點時間，Windows 的錯誤回報也會插一腳
    const DWORD waited = WaitForSingleObject(process.hProcess, 60000);
    DWORD code = 0;
    if (waited == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &code);
    } else {
        TerminateProcess(process.hProcess, 1);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code;
}

std::vector<std::filesystem::path> dumpsIn(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> found;
    std::error_code ignored;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory, ignored)) {
        if (entry.path().extension() == L".dmp") {
            found.push_back(entry.path());
        }
    }
    return found;
}

class CrashDumpIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        directory_ = std::filesystem::temp_directory_path() /
                     (L"tmw-crash-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                      std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(directory_);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    std::filesystem::path directory_;
};

TEST_F(CrashDumpIntegrationTest, ACrashedProcessLeavesADumpBehind) {
    ASSERT_TRUE(std::filesystem::exists(crashHelperPath()))
        << "找不到 " << crashHelperPath().string();

    const std::filesystem::path dumps = platform::dumpsDirectory(directory_);
    const DWORD code = runCrashHelper(dumps);
    // 存取違規：程式一定不是正常結束的
    EXPECT_NE(code, 0u) << "那個程式應該要當掉才對";
    EXPECT_NE(code, 3u) << "裝不上處理常式";

    const std::vector<std::filesystem::path> found = dumpsIn(dumps);
    ASSERT_EQ(found.size(), 1u) << "當掉之後應該剛好留下一個傾印檔";
    EXPECT_GE(std::filesystem::file_size(found[0]), 4096u);
    EXPECT_TRUE(found[0].filename().wstring().starts_with(L"crash-"));
}

}  // namespace
}  // namespace tmw
