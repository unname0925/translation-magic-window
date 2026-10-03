// M5-04：檢查有沒有新版本。用假的 HTTP 用戶端，不連網路。
#include "net/update_check.h"

#include <gtest/gtest.h>

#include "support/fake_http_client.h"

namespace tmw::net {
namespace {

TEST(UpdateCheckTest, ComparesVersions) {
    EXPECT_TRUE(isNewerVersion("0.2.0", "0.1.0"));
    EXPECT_TRUE(isNewerVersion("v0.10.0", "0.9.3")) << "逐段比數字，不是比字串";
    EXPECT_TRUE(isNewerVersion("1.0", "0.9.9")) << "少的部分補 0";
    EXPECT_FALSE(isNewerVersion("0.1.0", "0.1.0"));
    EXPECT_FALSE(isNewerVersion("0.0.9", "0.1.0"));
    EXPECT_FALSE(isNewerVersion("0.2.0-beta", "0.1.0")) << "預覽版不通知";
    EXPECT_FALSE(isNewerVersion("最新", "0.1.0")) << "看不懂的不打擾使用者";
}

TEST(UpdateCheckTest, ReadsTheLatestRelease) {
    test::FakeHttpClient http;
    http.reply(R"({"tag_name": "v0.2.0", "draft": false, "prerelease": false,
        "html_url": "https://github.com/unname0925/translation-magic-window/releases/tag/v0.2.0"})");
    const auto release = fetchLatestRelease(http, {});
    ASSERT_TRUE(release.has_value());
    EXPECT_EQ(release->version, "0.2.0");
    EXPECT_EQ(release->url,
              "https://github.com/unname0925/translation-magic-window/releases/tag/v0.2.0");
    ASSERT_EQ(http.requests.size(), 1u);
    EXPECT_EQ(http.requests[0].url, kLatestReleaseUrl);
    EXPECT_EQ(http.requests[0].method, HttpMethod::Get);
    EXPECT_TRUE(http.requests[0].body.empty()) << "只讀公開資訊，什麼都不送";
}

TEST(UpdateCheckTest, NoReleaseYetOrNoConnectionMeansNothing) {
    test::FakeHttpClient http;
    http.reply(R"({"message": "Not Found"})", 404);
    EXPECT_FALSE(fetchLatestRelease(http, {}).has_value()) << "還沒有發行過";
    http.failToConnect();
    EXPECT_FALSE(fetchLatestRelease(http, {}).has_value());
    http.reply("<html>rate limited</html>");
    EXPECT_FALSE(fetchLatestRelease(http, {}).has_value()) << "看不懂的回應";
    http.reply(R"({"tag_name": "v0.3.0", "prerelease": true, "html_url": "x"})");
    EXPECT_FALSE(fetchLatestRelease(http, {}).has_value()) << "預覽版不通知";
}

}  // namespace
}  // namespace tmw::net
