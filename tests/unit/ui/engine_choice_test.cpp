// 設定畫面的「翻譯引擎」和設定檔中的引擎鏈怎麼對應（M1-13）。
#include "ui/engine_choice.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace tmw::ui {
namespace {

core::Settings withEngines(std::vector<core::EngineSettings> engines) {
    core::Settings settings;
    settings.engines = std::move(engines);
    return settings;
}

std::vector<std::string> idsOf(const std::vector<core::EngineSettings>& engines) {
    std::vector<std::string> ids;
    for (const core::EngineSettings& engine : engines) {
        ids.push_back(engine.id);
    }
    return ids;
}

TEST(EngineChoiceTest, NoEnginesMeansGoogleOnly) {
    const EngineChoice choice = engineChoiceFrom(core::Settings{});
    EXPECT_FALSE(choice.useLlm);
    EXPECT_FALSE(choice.hasKey);
}

TEST(EngineChoiceTest, ReadsTheLlmSettings) {
    const EngineChoice choice = engineChoiceFrom(
        withEngines({{"openai-compatible", "http://127.0.0.1:11434/v1", "hy-mt2", "加密過的金鑰"},
                     {"google", "", "", ""}}));
    EXPECT_TRUE(choice.useLlm);
    EXPECT_EQ(choice.endpoint, "http://127.0.0.1:11434/v1");
    EXPECT_EQ(choice.model, "hy-mt2");
    EXPECT_TRUE(choice.hasKey);
    EXPECT_TRUE(choice.fallbackToGoogle);
}

TEST(EngineChoiceTest, NoticesWhenThereIsNoFallback) {
    const EngineChoice choice =
        engineChoiceFrom(withEngines({{"openai-compatible", "", "hy-mt2", ""}}));
    EXPECT_TRUE(choice.useLlm);
    EXPECT_FALSE(choice.fallbackToGoogle);
}

TEST(EngineChoiceTest, GoogleOnlyWritesOneEngine) {
    EngineChoice choice;
    choice.useLlm = false;
    EXPECT_EQ(idsOf(enginesFor(choice, core::Settings{}, "")),
              (std::vector<std::string>{"google"}));
}

TEST(EngineChoiceTest, LlmComesBeforeGoogle) {
    EngineChoice choice;
    choice.useLlm = true;
    choice.endpoint = "http://127.0.0.1:11434/v1";
    choice.model = "hy-mt2";
    choice.fallbackToGoogle = true;
    const std::vector<core::EngineSettings> engines = enginesFor(choice, core::Settings{}, "");
    EXPECT_EQ(idsOf(engines), (std::vector<std::string>{"openai-compatible", "google"}));
    EXPECT_EQ(engines[0].endpoint, "http://127.0.0.1:11434/v1");
    EXPECT_EQ(engines[0].model, "hy-mt2");
}

TEST(EngineChoiceTest, WithoutFallbackOnlyTheLlm) {
    EngineChoice choice;
    choice.useLlm = true;
    choice.fallbackToGoogle = false;
    EXPECT_EQ(idsOf(enginesFor(choice, core::Settings{}, "")),
              (std::vector<std::string>{"openai-compatible"}));
}

TEST(EngineChoiceTest, AnEmptyKeyKeepsTheStoredOne) {
    // 已經存過的金鑰不會被讀出來顯示，所以留空代表「不更改」
    const core::Settings current =
        withEngines({{"openai-compatible", "", "hy-mt2", "舊的加密金鑰"}});
    EngineChoice choice = engineChoiceFrom(current);
    const std::vector<core::EngineSettings> engines = enginesFor(choice, current, "");
    ASSERT_FALSE(engines.empty());
    EXPECT_EQ(engines[0].encryptedApiKey, "舊的加密金鑰");
}

TEST(EngineChoiceTest, ANewKeyReplacesTheStoredOne) {
    const core::Settings current =
        withEngines({{"openai-compatible", "", "hy-mt2", "舊的加密金鑰"}});
    EngineChoice choice = engineChoiceFrom(current);
    const std::vector<core::EngineSettings> engines = enginesFor(choice, current, "新的加密金鑰");
    ASSERT_FALSE(engines.empty());
    EXPECT_EQ(engines[0].encryptedApiKey, "新的加密金鑰");
}

TEST(EngineChoiceTest, SwitchingToGoogleKeepsTheKeyOutOfTheFile) {
    // 改用 Google 之後設定檔裡不該還留著 LLM 的金鑰
    const core::Settings current =
        withEngines({{"openai-compatible", "", "hy-mt2", "加密過的金鑰"}, {"google", "", "", ""}});
    EngineChoice choice;
    choice.useLlm = false;
    const std::vector<core::EngineSettings> engines = enginesFor(choice, current, "");
    ASSERT_EQ(engines.size(), 1u);
    EXPECT_EQ(engines[0].id, "google");
}

TEST(EngineChoiceTest, RoundTripsThroughTheSettings) {
    const core::Settings current =
        withEngines({{"openai-compatible", "http://127.0.0.1:11434/v1", "hy-mt2", "金鑰"},
                     {"google", "", "", ""}});
    core::Settings written = current;
    written.engines = enginesFor(engineChoiceFrom(current), current, "");
    EXPECT_EQ(written.engines, current.engines);
}

}  // namespace
}  // namespace tmw::ui
