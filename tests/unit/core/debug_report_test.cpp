// 除錯傾印的內容（M1-14）。重點有兩個：金鑰不能出現，而且查問題要的東西都在。
#include "core/debug_report.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <string>

namespace tmw::core {
namespace {

using nlohmann::json;

OcrLine line(const std::string& text, RectI rect = {0, 0, 10, 10}) {
    OcrLine out;
    out.text = text;
    out.rect = rect;
    out.score = 0.9f;
    return out;
}

DebugReportInput inputWithAResult() {
    DebugReportInput input;
    input.appVersion = "0.1.0";
    input.time = "2026-09-22 13:45:01";
    input.ocrDevice = "DirectML";
    input.engineStatus = "google：可以使用";

    PipelineResult result;
    result.lens = 1;
    result.generation = 7;
    result.region = {100, 200, 400, 500};
    result.language = Language::Japanese;
    result.timings = {.ocrMs = 12.5, .layoutMs = 1.5, .translationMs = 300.0};

    TranslatedBlock group;
    group.block.text = "今日はいい天気";
    group.block.language = Language::Japanese;
    group.block.orientation = Orientation::Vertical;
    group.block.rect = {110, 210, 200, 400};
    group.block.ruby = {RubyAnnotation{0, 2, "きょう"}};
    group.block.lines = {line("今日はいい天気", {110, 210, 200, 400})};
    group.translation = "今天天氣真好";
    result.groups.push_back(group);

    input.lastResult = result;
    input.lastLines = {line("今日はいい天気"), line("きょう")};
    return input;
}

TEST(WithoutSecretsTest, ReplacesAStoredKey) {
    Settings settings;
    settings.engines = {{"openai-compatible", "", "hy-mt2", "加密過的金鑰"}};
    const Settings clean = withoutSecrets(settings);
    ASSERT_EQ(clean.engines.size(), 1u);
    EXPECT_EQ(clean.engines[0].encryptedApiKey, "（已移除）");
    EXPECT_EQ(clean.engines[0].model, "hy-mt2") << "其他欄位要留著";
}

TEST(WithoutSecretsTest, LeavesAnEmptyKeyEmpty) {
    // 「沒設定金鑰」和「有設定但被拿掉了」是兩回事，查問題時分得出來才有用
    Settings settings;
    settings.engines = {{"google", "", "", ""}};
    EXPECT_TRUE(withoutSecrets(settings).engines[0].encryptedApiKey.empty());
}

TEST(WithoutSecretsTest, DoesNotChangeTheOriginal) {
    Settings settings;
    settings.engines = {{"openai-compatible", "", "", "加密過的金鑰"}};
    const Settings clean = withoutSecrets(settings);
    EXPECT_EQ(settings.engines[0].encryptedApiKey, "加密過的金鑰") << "傳進去的那份不該被改";
    EXPECT_NE(clean.engines[0].encryptedApiKey, settings.engines[0].encryptedApiKey);
}

TEST(DebugReportTest, IsValidJson) {
    json parsed;
    EXPECT_NO_THROW(parsed = json::parse(buildDebugReport(inputWithAResult())));
    EXPECT_TRUE(parsed.is_object());
}

TEST(DebugReportTest, NeverContainsTheKey) {
    DebugReportInput input = inputWithAResult();
    input.settings.engines = {{"openai-compatible", "", "hy-mt2", "sk-秘密-12345"}};
    const std::string report = buildDebugReport(input);
    EXPECT_EQ(report.find("sk-秘密-12345"), std::string::npos)
        << "傾印是要寄給別人看的，金鑰不能在裡面";
    EXPECT_NE(report.find("（已移除）"), std::string::npos) << "但要看得出有設定過金鑰";
    EXPECT_NE(report.find("hy-mt2"), std::string::npos) << "用哪個模型是查問題要知道的";
}

TEST(DebugReportTest, SaysWhichVersionAndWhen) {
    const json report = json::parse(buildDebugReport(inputWithAResult()));
    EXPECT_EQ(report["app"]["version"], "0.1.0");
    EXPECT_EQ(report["app"]["time"], "2026-09-22 13:45:01");
    EXPECT_EQ(report["ocrDevice"], "DirectML");
    EXPECT_EQ(report["engineStatus"], "google：可以使用");
}

TEST(DebugReportTest, KeepsTheSourceAndTheTranslationTogether) {
    const json report = json::parse(buildDebugReport(inputWithAResult()));
    ASSERT_EQ(report["lastResult"]["groups"].size(), 1u);
    const json& group = report["lastResult"]["groups"][0];
    EXPECT_EQ(group["source"]["text"], "今日はいい天気");
    EXPECT_EQ(group["translation"], "今天天氣真好");
    EXPECT_EQ(group["source"]["orientation"], "vertical");
    EXPECT_EQ(group["source"]["language"], "ja");
}

TEST(DebugReportTest, KeepsTheRubyItFound) {
    const json report = json::parse(buildDebugReport(inputWithAResult()));
    const json& ruby = report["lastResult"]["groups"][0]["source"]["ruby"];
    ASSERT_EQ(ruby.size(), 1u);
    EXPECT_EQ(ruby[0]["reading"], "きょう");
    EXPECT_EQ(ruby[0]["start"], 0);
    EXPECT_EQ(ruby[0]["length"], 2);
}

TEST(DebugReportTest, KeepsTheLinesBeforeTheyWereMerged) {
    // 「為什麼這句被切成兩段」只有合併前的行看得出來
    const json report = json::parse(buildDebugReport(inputWithAResult()));
    ASSERT_EQ(report["ocrLines"].size(), 2u);
    EXPECT_EQ(report["ocrLines"][1]["text"], "きょう");
}

TEST(DebugReportTest, KeepsTheTimings) {
    const json report = json::parse(buildDebugReport(inputWithAResult()));
    const json& timings = report["lastResult"]["timings"];
    EXPECT_DOUBLE_EQ(timings["ocrMs"].get<double>(), 12.5);
    EXPECT_DOUBLE_EQ(timings["totalMs"].get<double>(), 314.0);
}

TEST(DebugReportTest, WorksBeforeAnythingHasBeenTranslated) {
    DebugReportInput input;
    input.appVersion = "0.1.0";
    const json report = json::parse(buildDebugReport(input));
    EXPECT_TRUE(report["lastResult"].is_null());
    EXPECT_TRUE(report["ocrLines"].empty());
}

TEST(DebugReportTest, SaysWhyTheTranslationFailed) {
    DebugReportInput input = inputWithAResult();
    input.lastResult->error = "所有引擎都暫停中";
    const json report = json::parse(buildDebugReport(input));
    EXPECT_EQ(report["lastResult"]["error"], "所有引擎都暫停中");
}

}  // namespace
}  // namespace tmw::core
