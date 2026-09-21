// UT-07（前半）：LRU 淘汰順序、正規化後相同的原文會命中同一筆。
#include "core/translation_cache.h"

#include <gtest/gtest.h>

#include <string>

namespace tmw::core {
namespace {

TranslationKey key(std::string text, std::string engine = "google") {
    return TranslationKey{std::move(engine), "ja", "zh-TW", std::move(text)};
}

TEST(NormalizeSourceTest, TrimsAndCollapsesWhitespace) {
    EXPECT_EQ(normalizeSource("  hello   world \n"), "hello world");
    EXPECT_EQ(normalizeSource("hello\tworld"), "hello world");
    EXPECT_EQ(normalizeSource("hello\r\nworld"), "hello world");
}

TEST(NormalizeSourceTest, HandlesWideSpaces) {
    EXPECT_EQ(normalizeSource("あ　い"), "あ い") << "全形空白 U+3000";
    EXPECT_EQ(normalizeSource("a b"), "a b") << "不換行空白";
    EXPECT_EQ(normalizeSource("　あ　"), "あ");
}

TEST(NormalizeSourceTest, DropsZeroWidthCharacters) {
    EXPECT_EQ(normalizeSource("﻿こんにちは"), "こんにちは");
    EXPECT_EQ(normalizeSource("こん​にちは"), "こんにちは");
}

TEST(NormalizeSourceTest, KeepsCaseAndPunctuation) {
    // 全大寫的遊戲介面和一般句子可能要翻得不一樣，不能混在一起
    EXPECT_NE(normalizeSource("SAVE"), normalizeSource("save"));
    EXPECT_EQ(normalizeSource("Hello, world!"), "Hello, world!");
}

TEST(TranslationCacheTest, ReturnsWhatWasStored) {
    TranslationCache cache(10);
    EXPECT_FALSE(cache.get(key("こんにちは")).has_value());
    cache.put(key("こんにちは"), "你好");
    EXPECT_EQ(cache.get(key("こんにちは")).value(), "你好");
    EXPECT_EQ(cache.size(), 1u);
}

TEST(TranslationCacheTest, NormalizedTextHitsTheSameEntry) {
    TranslationCache cache(10);
    cache.put(key("こんにちは"), "你好");
    EXPECT_EQ(cache.get(key("  こんにちは\n")).value(), "你好");
    EXPECT_EQ(cache.size(), 1u) << "只是空白不同，不該多存一筆";
}

TEST(TranslationCacheTest, SeparatesEnginesAndLanguages) {
    TranslationCache cache(10);
    cache.put(key("SAVE", "google"), "儲存");
    cache.put(key("SAVE", "openai"), "存檔");
    EXPECT_EQ(cache.get(key("SAVE", "google")).value(), "儲存");
    EXPECT_EQ(cache.get(key("SAVE", "openai")).value(), "存檔");

    TranslationKey korean = key("SAVE");
    korean.srcLang = "ko";
    EXPECT_FALSE(cache.get(korean).has_value()) << "來源語言不同就是不同的一筆";
}

TEST(TranslationCacheTest, FieldsCannotRunIntoEachOther) {
    // 「引擎 ab + 原文 c」和「引擎 a + 原文 bc」必須是不同的兩筆
    TranslationCache cache(10);
    TranslationKey first{"ab", "ja", "zh-TW", "c"};
    TranslationKey second{"a", "bja", "zh-TW", "c"};
    cache.put(first, "前者");
    EXPECT_FALSE(cache.get(second).has_value());
}

TEST(TranslationCacheTest, EvictsTheLeastRecentlyUsed) {
    TranslationCache cache(2);
    cache.put(key("一"), "1");
    cache.put(key("二"), "2");
    ASSERT_TRUE(cache.get(key("一")).has_value());  // 「一」變成最近用到的
    cache.put(key("三"), "3");

    EXPECT_EQ(cache.size(), 2u);
    EXPECT_TRUE(cache.get(key("一")).has_value());
    EXPECT_FALSE(cache.get(key("二")).has_value()) << "最久沒用到的是「二」";
    EXPECT_TRUE(cache.get(key("三")).has_value());
}

TEST(TranslationCacheTest, OverwritingDoesNotAddAnEntry) {
    TranslationCache cache(2);
    cache.put(key("一"), "1");
    cache.put(key("一"), "壹");
    EXPECT_EQ(cache.size(), 1u);
    EXPECT_EQ(cache.get(key("一")).value(), "壹");
}

TEST(TranslationCacheTest, ClearEmptiesEverything) {
    TranslationCache cache(10);
    cache.put(key("一"), "1");
    cache.clear();
    EXPECT_EQ(cache.size(), 0u);
    EXPECT_FALSE(cache.get(key("一")).has_value());
}

TEST(TranslationCacheTest, CapacityIsAtLeastOne) {
    TranslationCache cache(0);
    EXPECT_EQ(cache.capacity(), 1u);
    cache.put(key("一"), "1");
    EXPECT_EQ(cache.get(key("一")).value(), "1");
}

}  // namespace
}  // namespace tmw::core
