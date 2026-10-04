#include "core/engine_history.h"

#include <gtest/gtest.h>

namespace tmw::core {
namespace {

EngineSettings ollama(std::string model) {
    EngineSettings engine;
    engine.id = "openai-compatible";
    engine.endpoint = "http://127.0.0.1:11434/v1";
    engine.model = std::move(model);
    return engine;
}

EngineSettings claude() {
    EngineSettings engine;
    engine.id = "anthropic";
    engine.model = "claude-haiku-4-5";
    engine.encryptedApiKey = "secret";
    return engine;
}

EngineSettings google() {
    EngineSettings engine;
    engine.id = "google";
    return engine;
}

TEST(EngineHistoryTest, TheCurrentEngineGoesToTheFront) {
    Settings settings;
    settings.engines = {ollama("hy-mt2"), google()};
    rememberCurrentEngine(settings);
    settings.engines = {claude(), google()};
    rememberCurrentEngine(settings);
    ASSERT_EQ(settings.engineHistory.size(), 2u);
    EXPECT_EQ(settings.engineHistory[0].id, "anthropic");
    EXPECT_EQ(settings.engineHistory[1].model, "hy-mt2");
}

TEST(EngineHistoryTest, TheSameEngineIsNotListedTwice) {
    Settings settings;
    settings.engines = {ollama("hy-mt2")};
    rememberCurrentEngine(settings);
    settings.engines = {claude()};
    rememberCurrentEngine(settings);
    settings.engines = {ollama("hy-mt2")};
    rememberCurrentEngine(settings);
    ASSERT_EQ(settings.engineHistory.size(), 2u);
    EXPECT_EQ(settings.engineHistory[0].model, "hy-mt2");
}

TEST(EngineHistoryTest, KeepsAtMostTen) {
    Settings settings;
    for (int i = 0; i < 15; ++i) {
        settings.engines = {ollama("m" + std::to_string(i))};
        rememberCurrentEngine(settings);
    }
    ASSERT_EQ(settings.engineHistory.size(), kEngineHistoryLimit);
    EXPECT_EQ(settings.engineHistory[0].model, "m14");
}

TEST(EngineHistoryTest, SwitchingKeepsTheKeyAndTheGoogleFallback) {
    Settings settings;
    settings.engines = {claude(), google()};
    rememberCurrentEngine(settings);
    settings.engines = {ollama("hy-mt2"), google()};
    rememberCurrentEngine(settings);
    // 歷史：[hy-mt2, claude]；改回 Claude
    ASSERT_TRUE(useEngineFromHistory(settings, 1));
    ASSERT_EQ(settings.engines.size(), 2u);
    EXPECT_EQ(settings.engines[0].id, "anthropic");
    EXPECT_EQ(settings.engines[0].encryptedApiKey, "secret") << "金鑰跟著歷史一起帶回來";
    EXPECT_EQ(settings.engines[1].id, "google");
    EXPECT_EQ(settings.engineHistory[0].id, "anthropic") << "用過的移到最前面";
}

TEST(EngineHistoryTest, NoFallbackStaysWithout) {
    Settings settings;
    settings.engines = {claude()};
    rememberCurrentEngine(settings);
    settings.engines = {ollama("hy-mt2")};
    rememberCurrentEngine(settings);
    ASSERT_TRUE(useEngineFromHistory(settings, 1));
    EXPECT_EQ(settings.engines.size(), 1u);
}

TEST(EngineHistoryTest, ChoosingGoogleLeavesOnlyGoogle) {
    Settings settings;
    settings.engines = {google()};
    rememberCurrentEngine(settings);
    settings.engines = {ollama("hy-mt2"), google()};
    rememberCurrentEngine(settings);
    ASSERT_TRUE(useEngineFromHistory(settings, 1));
    ASSERT_EQ(settings.engines.size(), 1u);
    EXPECT_EQ(settings.engines[0].id, "google");
}

TEST(EngineHistoryTest, AnIndexOutOfRangeChangesNothing) {
    Settings settings;
    settings.engines = {ollama("hy-mt2")};
    const Settings before = settings;
    EXPECT_FALSE(useEngineFromHistory(settings, 3));
    EXPECT_EQ(settings, before);
}

TEST(EngineHistoryTest, LabelsAreReadable) {
    EXPECT_EQ(engineLabel(ollama("hy-mt2-tmw")), "hy-mt2-tmw（Ollama 127.0.0.1:11434）");
    EXPECT_EQ(engineLabel(google()), "Google 翻譯（免費）");
    EXPECT_EQ(engineLabel(claude()), "claude-haiku-4-5（Claude）");
}

TEST(EngineHistoryTest, SurvivesASaveAndLoad) {
    Settings settings;
    settings.engines = {ollama("hy-mt2")};
    settings.engineHistory = {ollama("hy-mt2"), claude()};
    const Settings loaded = parseSettings(serializeSettings(settings)).settings;
    EXPECT_EQ(loaded.engineHistory, settings.engineHistory);
}

}  // namespace
}  // namespace tmw::core
