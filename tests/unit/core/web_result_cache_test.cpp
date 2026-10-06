// 網頁漫畫結果的硬碟快取：存取、鍵、容量上限、重開之後還在、換成這次的 id。
#include "core/web_result_cache.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "core/web_protocol.h"

namespace tmw::core {
namespace {

class WebResultCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        directory_ =
            std::filesystem::temp_directory_path() /
            ("tmw-web-cache-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
             "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(directory_);
    }
    void TearDown() override { std::filesystem::remove_all(directory_); }

    std::filesystem::path directory_;
};

std::string reply(const std::string& id, const std::string& text) {
    return R"({"type":"result","id":")" + id + R"(","items":[{"text":")" + text + R"("}]})";
}

TEST_F(WebResultCacheTest, StoresAndFindsAResult) {
    WebResultCache cache(directory_, 1 << 20);
    const std::string key = WebResultCache::makeKey(42, 800, 1200, "ja", true, "tag");
    EXPECT_FALSE(cache.find(key).has_value());
    cache.store(key, reply("first", "你好"));
    ASSERT_EQ(cache.find(key), reply("first", "你好"));
}

TEST_F(WebResultCacheTest, KeysDependOnEverythingThatChangesTheResult) {
    const std::string base = WebResultCache::makeKey(42, 800, 1200, "ja", true, "tag");
    EXPECT_EQ(base.size(), 16u);
    EXPECT_NE(base, WebResultCache::makeKey(43, 800, 1200, "ja", true, "tag"));
    EXPECT_NE(base, WebResultCache::makeKey(42, 800, 1201, "ja", true, "tag"));
    EXPECT_NE(base, WebResultCache::makeKey(42, 800, 1200, "auto", true, "tag"));
    EXPECT_NE(base, WebResultCache::makeKey(42, 800, 1200, "ja", false, "tag"));
    EXPECT_NE(base, WebResultCache::makeKey(42, 800, 1200, "ja", true, "other engine"));
    EXPECT_EQ(base, WebResultCache::makeKey(42, 800, 1200, "ja", true, "tag"));
}

TEST_F(WebResultCacheTest, SurvivesARestart) {
    const std::string key = WebResultCache::makeKey(7, 10, 10, "ja", true, "tag");
    WebResultCache(directory_, 1 << 20).store(key, reply("a", "x"));
    WebResultCache reopened(directory_, 1 << 20);
    EXPECT_EQ(reopened.find(key), reply("a", "x"));
    EXPECT_GT(reopened.totalBytes(), 0u);
}

TEST_F(WebResultCacheTest, DropsTheLeastRecentlyUsedWhenFull) {
    const std::string big(400, 'x');
    WebResultCache cache(directory_, 1000);
    const std::string a = WebResultCache::makeKey(1, 1, 1, "", true, "");
    const std::string b = WebResultCache::makeKey(2, 1, 1, "", true, "");
    const std::string c = WebResultCache::makeKey(3, 1, 1, "", true, "");
    cache.store(a, big);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    cache.store(b, big);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT_TRUE(cache.find(a).has_value());  // a 剛用過，b 變成最久沒用的
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    cache.store(c, big);  // 1200 > 1000：刪掉一個
    EXPECT_TRUE(cache.find(a).has_value());
    EXPECT_FALSE(cache.find(b).has_value());
    EXPECT_TRUE(cache.find(c).has_value());
    EXPECT_LE(cache.totalBytes(), 1000u);
}

TEST_F(WebResultCacheTest, ThePixelHashFollowsThePixels) {
    ImageBgra image;
    image.width = 2;
    image.height = 1;
    image.pixels = {1, 2, 3, 255, 4, 5, 6, 255};
    const std::uint64_t first = hashPixels(image);
    EXPECT_EQ(first, hashPixels(image));
    image.pixels[0] = 9;
    EXPECT_NE(first, hashPixels(image));
}

TEST(WebReplyWithIdTest, ReplacesTheIdOfAStoredResult) {
    // 重新編碼時欄位順序可能不同：比 JSON 的內容
    EXPECT_EQ(nlohmann::json::parse(webReplyWithId(reply("old", "x"), "new")),
              nlohmann::json::parse(reply("new", "x")));
    EXPECT_EQ(webReplyWithId(R"({"type":"error","id":"a","message":"m"})", "b"), "");
    EXPECT_EQ(webReplyWithId("not json", "b"), "");
}

}  // namespace
}  // namespace tmw::core
