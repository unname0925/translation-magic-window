// 本機 Ollama 的模型有沒有整個放在顯示卡上、沒放滿時卸載再載入。用假的 HTTP 用戶端，不連網路。
#include "net/ollama_status.h"

#include <gtest/gtest.h>

#include "support/fake_http_client.h"

namespace tmw::net {
namespace {

constexpr const char* kPartial =
    R"({"models":[{"name":"hy-mt2-tmw:latest","model":"hy-mt2-tmw:latest","size":5511315456,"size_vram":3880000000}]})";
constexpr const char* kFull =
    R"({"models":[{"name":"hy-mt2-tmw:latest","model":"hy-mt2-tmw:latest","size":5511315456,"size_vram":5511315456}]})";

TEST(OllamaStatusTest, ReadsHowMuchOfTheModelIsOnTheGpu) {
    const auto placement = parseOllamaPlacement(kPartial, "hy-mt2-tmw");
    ASSERT_TRUE(placement.has_value()) << "沒寫標籤就是 :latest";
    EXPECT_EQ(placement->size, 5511315456u);
    EXPECT_EQ(placement->onGpu, 3880000000u);
    EXPECT_FALSE(placement->fullyOnGpu());
    EXPECT_TRUE(parseOllamaPlacement(kFull, "hy-mt2-tmw:latest")->fullyOnGpu());
    EXPECT_FALSE(parseOllamaPlacement(kFull, "qwen3:8b").has_value()) << "別的模型";
    EXPECT_FALSE(parseOllamaPlacement(R"({"models":[]})", "hy-mt2-tmw").has_value()) << "沒載入";
    EXPECT_FALSE(parseOllamaPlacement("不是 JSON", "hy-mt2-tmw").has_value());
}

TEST(OllamaStatusTest, AModelOnlyPartlyOnTheGpuIsReloaded) {
    test::FakeHttpClient http;
    http.reply(kPartial);  // /api/ps
    http.reply("{}");      // 卸載
    http.reply("{}");      // 載入
    http.reply(kFull);     // /api/ps
    const OllamaPlacementCheck check =
        checkOllamaPlacement(http, "hy-mt2-tmw:latest", "30m", true, {});
    EXPECT_EQ(check.action, OllamaPlacementAction::Reloaded);
    ASSERT_EQ(http.requests.size(), 4u);
    EXPECT_EQ(http.requests[1].url, "http://127.0.0.1:11434/api/generate");
    EXPECT_NE(http.requests[1].body.find(R"("keep_alive":0)"), std::string::npos)
        << "數字 0：馬上卸載";
    EXPECT_NE(http.requests[2].body.find(R"("keep_alive":"30m")"), std::string::npos);
}

TEST(OllamaStatusTest, NothingIsDoneWhenTheModelFitsOrIsNotLoaded) {
    test::FakeHttpClient http;
    http.reply(kFull);
    EXPECT_EQ(checkOllamaPlacement(http, "hy-mt2-tmw", "30m", true, {}).action,
              OllamaPlacementAction::Full);
    http.reply(R"({"models":[]})");
    EXPECT_EQ(checkOllamaPlacement(http, "hy-mt2-tmw", "30m", true, {}).action,
              OllamaPlacementAction::NotLoaded);
    http.reply(kPartial);
    EXPECT_EQ(checkOllamaPlacement(http, "hy-mt2-tmw", "30m", false, {}).action,
              OllamaPlacementAction::StillPartial)
        << "不准重新載入（正在翻譯）時只回報";
    EXPECT_EQ(http.requests.size(), 3u);
}

TEST(OllamaStatusTest, StillPartialWhenOtherProgramsHoldTheMemory) {
    test::FakeHttpClient http;
    http.reply(kPartial);
    http.reply("{}");
    http.reply("{}");
    http.reply(kPartial);
    const OllamaPlacementCheck check = checkOllamaPlacement(http, "hy-mt2-tmw", "30m", true, {});
    EXPECT_EQ(check.action, OllamaPlacementAction::StillPartial);
    ASSERT_TRUE(check.after.has_value());
}

}  // namespace
}  // namespace tmw::net
