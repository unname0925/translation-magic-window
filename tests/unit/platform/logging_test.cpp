// M1-02：記錄系統。重點是隱私——預設不可以寫入擷取到的文字、譯文和金鑰（design.md 4.11）。
#include "platform/logging.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace tmw::platform {
namespace {

class LoggingTest : public testing::Test {
protected:
    void SetUp() override {
        directory_ = std::filesystem::temp_directory_path() / "tmw-logging-test" /
                     testing::UnitTest::GetInstance()->current_test_info()->name();
        std::filesystem::remove_all(directory_);
    }

    void TearDown() override {
        shutdownLogging();
        std::filesystem::remove_all(directory_);
    }

    void start(bool verbose) {
        initializeLogging(
            {.directory = directory_, .level = LogLevel::Trace, .verboseDiagnostics = verbose});
    }

    std::string contents() {
        shutdownLogging();  // 先把緩衝寫出去
        std::ifstream file(directory_ / "translation-magic-window.log", std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        return text.str();
    }

    std::filesystem::path directory_;
};

TEST_F(LoggingTest, WritesMessagesToTheLogFile) {
    start(false);

    logInfo("開始擷取");

    EXPECT_NE(contents().find("開始擷取"), std::string::npos);
}

TEST_F(LoggingTest, KeepsCapturedTextOutOfTheLogByDefault) {
    start(false);

    log(LogLevel::Info, "辨識結果：" + sensitive("えーマジで?てかさー"));
    log(LogLevel::Info, "金鑰：" + sensitive("sk-ant-api03-0123456789"));

    const std::string text = contents();
    EXPECT_EQ(text.find("えーマジで"), std::string::npos) << "擷取到的文字不可以寫進記錄檔";
    EXPECT_EQ(text.find("sk-ant-api03"), std::string::npos) << "金鑰不可以寫進記錄檔";
    EXPECT_NE(text.find("辨識結果："), std::string::npos) << "非敏感的部分還是要記錄";
}

TEST_F(LoggingTest, WritesSensitiveTextOnlyWithVerboseDiagnostics) {
    start(true);

    log(LogLevel::Info, "辨識結果：" + sensitive("えーマジで?てかさー"));

    EXPECT_NE(contents().find("えーマジで?てかさー"), std::string::npos);
}

TEST_F(LoggingTest, SensitiveIsRedactedWhenLoggingIsNotInitialized) {
    // 還沒初始化時（例如啟動失敗的路徑）也不可以洩漏
    EXPECT_EQ(sensitive("秘密"), "（略）");
}

TEST_F(LoggingTest, RecordsTheLensAndSequenceNumber) {
    start(false);

    log(LogLevel::Info, {.lens = 2, .sequence = 417}, "翻譯完成");

    const std::string text = contents();
    EXPECT_NE(text.find("[透鏡 2 #417] 翻譯完成"), std::string::npos);
}

TEST_F(LoggingTest, MessagesBelowTheConfiguredLevelAreSkipped) {
    initializeLogging({.directory = directory_, .level = LogLevel::Warn});

    logInfo("這一筆不該出現");
    logWarn("這一筆要出現");

    const std::string text = contents();
    EXPECT_EQ(text.find("這一筆不該出現"), std::string::npos);
    EXPECT_NE(text.find("這一筆要出現"), std::string::npos);
}

TEST_F(LoggingTest, TreatsBracesInMessagesAsText) {
    // 訊息裡可能有 JSON，不可以被當成格式字串
    start(false);

    logInfo(R"({"segments": ["{本気|マジ}"]})");

    EXPECT_NE(contents().find(R"({"segments": ["{本気|マジ}"]})"), std::string::npos);
}

}  // namespace
}  // namespace tmw::platform
