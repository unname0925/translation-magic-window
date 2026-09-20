// UT-09：設定檔（見 docs/execution-plan.md 5.3）。
#include "core/settings.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <vector>

namespace tmw::core {
namespace {

TEST(SettingsTest, ReadsEveryField) {
    const SettingsLoad load = parseSettings(R"({
      "schemaVersion": 1,
      "verboseDiagnostics": true,
      "engines": [
        {"id": "google", "endpoint": "", "model": "", "encryptedApiKey": ""},
        {"id": "openai-compatible", "endpoint": "http://127.0.0.1:11434/v1",
         "model": "hy-mt2", "encryptedApiKey": "AQID"}
      ],
      "resultWindow": {"fontScale": 1.5, "alwaysOnTop": true, "theme": "dark"}
    })");

    ASSERT_FALSE(load.usedDefaults()) << load.problem;
    EXPECT_TRUE(load.settings.verboseDiagnostics);
    ASSERT_EQ(load.settings.engines.size(), 2u);
    EXPECT_EQ(load.settings.engines[1].endpoint, "http://127.0.0.1:11434/v1");
    EXPECT_EQ(load.settings.engines[1].encryptedApiKey, "AQID");
    EXPECT_EQ(load.settings.resultWindow.fontScale, 1.5);
    EXPECT_TRUE(load.settings.resultWindow.alwaysOnTop);
    EXPECT_EQ(load.settings.resultWindow.theme, "dark");
}

TEST(SettingsTest, MissingFieldsKeepTheirDefaults) {
    const SettingsLoad load = parseSettings(R"({"schemaVersion": 1})");

    ASSERT_FALSE(load.usedDefaults()) << load.problem;
    EXPECT_EQ(load.settings, Settings{});
}

TEST(SettingsTest, FieldsWithTheWrongTypeKeepTheirDefaults) {
    // 使用者可以手改設定檔，單一欄位壞掉不該讓整份設定作廢
    const SettingsLoad load = parseSettings(R"({
      "schemaVersion": 1,
      "verboseDiagnostics": "yes",
      "engines": {"id": "google"},
      "resultWindow": {"fontScale": "大", "theme": "螢光綠"}
    })");

    ASSERT_FALSE(load.usedDefaults()) << load.problem;
    EXPECT_EQ(load.settings, Settings{});
}

TEST(SettingsTest, FontScaleIsClampedToTheSupportedRange) {
    EXPECT_EQ(
        parseSettings(R"({"resultWindow": {"fontScale": 99}})").settings.resultWindow.fontScale,
        3.0);
    EXPECT_EQ(
        parseSettings(R"({"resultWindow": {"fontScale": 0}})").settings.resultWindow.fontScale,
        0.5);
}

TEST(SettingsTest, BrokenJsonUsesDefaultsAndExplainsWhy) {
    const SettingsLoad load = parseSettings("{ 這不是 JSON");

    EXPECT_TRUE(load.usedDefaults());
    EXPECT_EQ(load.settings, Settings{});
    EXPECT_FALSE(load.problem.empty());
}

TEST(SettingsTest, ANewerSchemaUsesDefaultsInsteadOfGuessing) {
    const SettingsLoad load = parseSettings(R"({"schemaVersion": 99, "verboseDiagnostics": true})");

    EXPECT_TRUE(load.usedDefaults());
    EXPECT_FALSE(load.settings.verboseDiagnostics);
}

TEST(SettingsTest, MigrationsRunInOrder) {
    // 用假的遷移函式檢查機制：版本 1 的檔案要依序經過兩次遷移才變成版本 3 的內容
    std::vector<SettingsMigration> migrations;
    migrations.push_back([](nlohmann::json& document) {
        document["verboseDiagnostics"] = document.value("verbose", false);
        document.erase("verbose");
    });
    migrations.push_back([](nlohmann::json& document) {
        document["resultWindow"]["theme"] = document.value("darkMode", false) ? "dark" : "light";
        document.erase("darkMode");
    });

    const SettingsLoad load =
        parseSettings(R"({"schemaVersion": 1, "verbose": true, "darkMode": true})", migrations);

    ASSERT_FALSE(load.usedDefaults()) << load.problem;
    EXPECT_TRUE(load.settings.verboseDiagnostics);
    EXPECT_EQ(load.settings.resultWindow.theme, "dark");
}

TEST(SettingsTest, MigrationsOnlyRunForOlderFiles) {
    std::vector<SettingsMigration> migrations;
    migrations.push_back([](nlohmann::json& document) { document["verboseDiagnostics"] = true; });

    const SettingsLoad load = parseSettings(R"({"schemaVersion": 2})", migrations);

    ASSERT_FALSE(load.usedDefaults()) << load.problem;
    EXPECT_FALSE(load.settings.verboseDiagnostics);
}

TEST(SettingsTest, WrittenSettingsCanBeReadBack) {
    Settings settings;
    settings.verboseDiagnostics = true;
    settings.engines = {{.id = "google"},
                        {.id = "claude", .model = "claude-haiku-4-5", .encryptedApiKey = "AQID"}};
    settings.resultWindow = {.fontScale = 1.25, .alwaysOnTop = true, .theme = "light"};

    const SettingsLoad load = parseSettings(serializeSettings(settings));

    ASSERT_FALSE(load.usedDefaults()) << load.problem;
    EXPECT_EQ(load.settings, settings);
}

TEST(SettingsTest, DefaultsSurviveAWriteAndReadCycle) {
    const SettingsLoad load = parseSettings(serializeSettings(Settings{}));

    ASSERT_FALSE(load.usedDefaults()) << load.problem;
    EXPECT_EQ(load.settings, Settings{});
}

}  // namespace
}  // namespace tmw::core
