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

TEST(SettingsTest, MangaModeIsOffUnlessTurnedOn) {
    // 舊的設定檔沒有這個欄位：一定是關閉的，不能因為升級就突然多跑一個模型
    EXPECT_FALSE(parseSettings(R"({"schemaVersion": 1})").settings.mangaMode);
    EXPECT_TRUE(parseSettings(R"({"schemaVersion": 1, "mangaMode": true})").settings.mangaMode);
}

TEST(SettingsTest, MangaModeSurvivesARoundTrip) {
    Settings settings;
    settings.mangaMode = true;
    EXPECT_TRUE(parseSettings(serializeSettings(settings)).settings.mangaMode);
}

TEST(SettingsTest, HotkeysDefaultToTheOriginalKeysAndSurviveARoundTrip) {
    // M2-10：舊的設定檔沒有 hotkeys，維持原本寫死的 Ctrl+Alt+Shift+T／D／S
    const Settings defaults = parseSettings(R"({"schemaVersion": 1})").settings;
    EXPECT_EQ(defaults.hotkeys.translate, "Ctrl+Alt+Shift+T");
    EXPECT_EQ(defaults.hotkeys.debugDump, "Ctrl+Alt+Shift+D");
    EXPECT_EQ(defaults.hotkeys.capture, "Ctrl+Alt+Shift+S");

    Settings settings;
    settings.hotkeys.translate = "Ctrl+Alt+F9";
    settings.hotkeys.capture = "";  // 不使用
    EXPECT_EQ(parseSettings(serializeSettings(settings)).settings.hotkeys, settings.hotkeys);
}

// M2-06：情境模式
TEST(ProfileTest, NoProfileMeansTheOldBehaviour) {
    const Settings settings = parseSettings(R"({"schemaVersion": 1, "mangaMode": true})").settings;
    EXPECT_EQ(settings.profile, "");
    EXPECT_TRUE(settings.mangaMode) << "舊的設定檔照樣讀";
    EXPECT_TRUE(settings.dropEdgeBlocks);
    EXPECT_EQ(settings.settleMs, 400);
}

TEST(ProfileTest, AFirstVisitUsesTheBuiltInValues) {
    Settings settings;
    switchProfile(settings, "game");
    EXPECT_EQ(settings.profile, "game");
    EXPECT_TRUE(settings.gameMode);
    EXPECT_FALSE(settings.mangaMode);
    EXPECT_FALSE(settings.dropEdgeBlocks) << "遊戲的對話框常貼著透鏡邊";
    EXPECT_EQ(settings.settleMs, 600);

    switchProfile(settings, "manga");
    EXPECT_TRUE(settings.mangaMode);
    EXPECT_FALSE(settings.gameMode);
    EXPECT_EQ(settings.settleMs, 300);
}

TEST(ProfileTest, EachProfileRemembersWhatWasChangedInIt) {
    Settings settings;
    switchProfile(settings, "manga");
    settings.ocrLanguage = "ja";  // 在漫畫情境裡指定日文
    switchProfile(settings, "web");
    EXPECT_EQ(settings.ocrLanguage, "auto") << "網頁情境還是自己的設定";
    switchProfile(settings, "manga");
    EXPECT_EQ(settings.ocrLanguage, "ja") << "切回漫畫時恢復";
}

TEST(ProfileTest, LeavingProfilesKeepsTheCurrentValues) {
    Settings settings;
    switchProfile(settings, "game");
    switchProfile(settings, "");
    EXPECT_EQ(settings.profile, "");
    EXPECT_TRUE(settings.gameMode) << "不用情境時維持現在的值，不會突然變回別的";
}

TEST(ProfileTest, ProfilesSurviveARoundTrip) {
    Settings settings;
    switchProfile(settings, "manga");
    settings.settleMs = 250;
    switchProfile(settings, "game");
    const Settings reloaded = parseSettings(serializeSettings(settings)).settings;
    EXPECT_EQ(reloaded.profile, "game");
    EXPECT_EQ(reloaded.profiles, settings.profiles);
    EXPECT_EQ(reloaded.profiles.at("manga").settleMs, 250);
}

TEST(ProfileTest, SettleTimeIsKeptInARange) {
    EXPECT_EQ(parseSettings(R"({"schemaVersion": 1, "settleMs": 5})").settings.settleMs, 100);
    EXPECT_EQ(parseSettings(R"({"schemaVersion": 1, "settleMs": 99999})").settings.settleMs, 3000);
}

TEST(SettingsTest, GameModeIsOffUnlessTurnedOn) {
    EXPECT_FALSE(parseSettings(R"({"schemaVersion": 1})").settings.gameMode);
    Settings settings;
    settings.gameMode = true;
    EXPECT_TRUE(parseSettings(serializeSettings(settings)).settings.gameMode);
}

TEST(SettingsTest, OverlayIsOffUnlessTurnedOn) {
    EXPECT_FALSE(parseSettings(R"({"schemaVersion": 1})").settings.overlay);
    Settings settings;
    settings.overlay = true;
    EXPECT_TRUE(parseSettings(serializeSettings(settings)).settings.overlay);
}

TEST(SettingsTest, OcrLanguageIsAutomaticUnlessChosen) {
    EXPECT_EQ(parseSettings(R"({"schemaVersion": 1})").settings.ocrLanguage, "auto");
    EXPECT_EQ(parseSettings(R"({"schemaVersion": 1, "ocrLanguage": "ko"})").settings.ocrLanguage,
              "ko");
}

TEST(SettingsTest, UnsupportedOcrLanguageFallsBackToAutomatic) {
    // 手改設定檔打錯字，或以後的版本多了這一版不認得的語言
    EXPECT_EQ(parseSettings(R"({"schemaVersion": 1, "ocrLanguage": "fr"})").settings.ocrLanguage,
              "auto");
    EXPECT_EQ(parseSettings(R"({"schemaVersion": 1, "ocrLanguage": 3})").settings.ocrLanguage,
              "auto");
}

TEST(SettingsTest, OcrLanguageSurvivesARoundTrip) {
    Settings settings;
    settings.ocrLanguage = "ja";
    EXPECT_EQ(parseSettings(serializeSettings(settings)).settings.ocrLanguage, "ja");
}

}  // namespace
}  // namespace tmw::core
