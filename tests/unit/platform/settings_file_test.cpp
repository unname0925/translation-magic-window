// UT-09 的檔案部分：壞掉的設定檔會被備份並改用預設值（見 docs/design.md 4.10）。
#include "platform/settings_file.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace tmw::platform {
namespace {

class SettingsFileTest : public testing::Test {
protected:
    void SetUp() override {
        directory_ = std::filesystem::temp_directory_path() / "tmw-settings-test" /
                     testing::UnitTest::GetInstance()->current_test_info()->name();
        std::filesystem::remove_all(directory_);
        path_ = settingsPathIn(directory_);
    }

    void TearDown() override { std::filesystem::remove_all(directory_); }

    void write(std::string_view text) {
        std::ofstream file(path_, std::ios::binary | std::ios::trunc);
        file << text;
    }

    std::string read(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        return text.str();
    }

    std::filesystem::path directory_;
    std::filesystem::path path_;
};

TEST_F(SettingsFileTest, FirstStartHasNoFileAndUsesDefaults) {
    const SettingsFileLoad load = loadSettings(path_);

    EXPECT_TRUE(load.missing);
    EXPECT_TRUE(load.problem.empty());
    EXPECT_EQ(load.settings, core::Settings{});
}

TEST_F(SettingsFileTest, SavesAndLoadsBack) {
    core::Settings settings;
    settings.verboseDiagnostics = true;
    settings.engines = {{.id = "google"}};
    saveSettings(path_, settings);

    const SettingsFileLoad load = loadSettings(path_);

    EXPECT_FALSE(load.missing);
    EXPECT_TRUE(load.problem.empty());
    EXPECT_EQ(load.settings, settings);
}

TEST_F(SettingsFileTest, BrokenFileIsBackedUpAndDefaultsAreUsed) {
    write("{ 使用者手改壞了");

    const SettingsFileLoad load = loadSettings(path_);

    EXPECT_FALSE(load.problem.empty());
    EXPECT_EQ(load.settings, core::Settings{});
    ASSERT_TRUE(load.backup.has_value());
    EXPECT_EQ(read(*load.backup), "{ 使用者手改壞了") << "原檔的內容要完整保留";
    EXPECT_FALSE(std::filesystem::exists(path_)) << "壞掉的檔案不該留在原位";
}

TEST_F(SettingsFileTest, ANewerSchemaIsAlsoBackedUp) {
    write(R"({"schemaVersion": 99})");

    const SettingsFileLoad load = loadSettings(path_);

    EXPECT_FALSE(load.problem.empty());
    ASSERT_TRUE(load.backup.has_value());
    EXPECT_EQ(read(*load.backup), R"({"schemaVersion": 99})");
}

TEST_F(SettingsFileTest, SavingAfterABrokenFileLeavesOneValidFile) {
    write("壞掉的內容");
    loadSettings(path_);

    saveSettings(path_, core::Settings{});

    const SettingsFileLoad load = loadSettings(path_);
    EXPECT_TRUE(load.problem.empty()) << load.problem;
    EXPECT_FALSE(load.missing);
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(path_) += L".tmp"))
        << "暫存檔要被取代掉";
}

TEST_F(SettingsFileTest, SettingsPathUsesTheDataDirectory) {
    EXPECT_EQ(settingsPathIn(directory_).parent_path(), directory_);
    EXPECT_EQ(settingsPathIn(directory_).filename(), "settings.json");
}

}  // namespace
}  // namespace tmw::platform
