// 除錯傾印的資料夾（M1-14）。驗收條件是「傾印的資料夾內容完整」，所以這裡真的寫出來再檢查。
#include "platform/debug_dump.h"

#include <windows.h>

#include <gtest/gtest.h>
#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>

namespace tmw::platform {
namespace {

using nlohmann::json;

std::chrono::system_clock::time_point aTime() {
    std::tm when{};
    when.tm_year = 2026 - 1900;
    when.tm_mon = 8;  // 9 月
    when.tm_mday = 22;
    when.tm_hour = 13;
    when.tm_min = 45;
    when.tm_sec = 1;
    when.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&when));
}

core::ImageBgra aFrame() {
    core::ImageBgra image(4, 2);
    std::ranges::fill(image.pixels, static_cast<std::uint8_t>(0x40));
    return image;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

class DebugDumpTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 存 PNG 走的是 WIC，需要 COM
        comResult_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        root_ = std::filesystem::temp_directory_path() /
                ("tmw-debugdump-" + std::to_string(GetCurrentProcessId()) + "-" +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_ / "logs");
        std::ofstream(root_ / "logs" / "translation-magic-window.log", std::ios::binary)
            << "記錄檔的內容\n";
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
        if (SUCCEEDED(comResult_)) {
            CoUninitialize();
        }
    }

    DebugDumpContents contents() const {
        DebugDumpContents out;
        out.report.appVersion = "0.1.0";
        out.report.time = "2026-09-22 13:45:01";
        out.report.ocrDevice = "DirectML";
        out.report.settings.engines = {{"openai-compatible", "", "hy-mt2", "sk-秘密-12345"}};
        out.logsDirectory = root_ / "logs";
        return out;
    }

    std::filesystem::path root_;
    HRESULT comResult_ = E_FAIL;
};

TEST_F(DebugDumpTest, TheFolderIsNamedAfterTheTime) {
    EXPECT_EQ(debugDumpFolderName(aTime()), L"debug-20260922-134501");
}

TEST_F(DebugDumpTest, HasEverythingNeededToLookIntoAProblem) {
    const core::ImageBgra frame = aFrame();
    DebugDumpContents what = contents();
    what.capture = &frame;

    const std::filesystem::path folder = writeDebugDump(root_ / "dumps", what, aTime());
    ASSERT_FALSE(folder.empty());
    EXPECT_EQ(folder.filename(), L"debug-20260922-134501");
    EXPECT_TRUE(std::filesystem::exists(folder / "report.json")) << "沒有報告";
    EXPECT_TRUE(std::filesystem::exists(folder / "capture.png")) << "沒有當時的畫面";
    EXPECT_TRUE(std::filesystem::exists(folder / "logs" / "translation-magic-window.log"))
        << "沒有記錄檔";
    EXPECT_GT(std::filesystem::file_size(folder / "capture.png"), 0u);
}

TEST_F(DebugDumpTest, TheReportIsReadableJsonWithoutTheKey) {
    const std::filesystem::path folder = writeDebugDump(root_ / "dumps", contents(), aTime());
    ASSERT_FALSE(folder.empty());
    const std::string text = readText(folder / "report.json");
    EXPECT_EQ(text.find("sk-秘密-12345"), std::string::npos) << "金鑰不能出現在傾印裡";

    const json report = json::parse(text);
    EXPECT_EQ(report["app"]["version"], "0.1.0");
    EXPECT_EQ(report["ocrDevice"], "DirectML");
}

TEST_F(DebugDumpTest, WorksWithoutAPictureOrLogs) {
    // 才剛啟動、還沒擷取過任何畫面時也要能傾印
    DebugDumpContents what = contents();
    what.logsDirectory.clear();
    const std::filesystem::path folder = writeDebugDump(root_ / "dumps", what, aTime());
    ASSERT_FALSE(folder.empty());
    EXPECT_TRUE(std::filesystem::exists(folder / "report.json"));
    EXPECT_FALSE(std::filesystem::exists(folder / "capture.png"));
    EXPECT_FALSE(std::filesystem::exists(folder / "logs"));
}

TEST_F(DebugDumpTest, TwoDumpsInTheSameSecondBothSurvive) {
    const std::filesystem::path first = writeDebugDump(root_ / "dumps", contents(), aTime());
    const std::filesystem::path second = writeDebugDump(root_ / "dumps", contents(), aTime());
    ASSERT_FALSE(first.empty());
    ASSERT_FALSE(second.empty());
    EXPECT_NE(first, second) << "第二次不能蓋掉第一次";
    EXPECT_TRUE(std::filesystem::exists(first / "report.json"));
    EXPECT_TRUE(std::filesystem::exists(second / "report.json"));
}

}  // namespace
}  // namespace tmw::platform
