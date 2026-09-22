// UT-08：只放新內容；沒有新內容時不新增卡片；超過上限時淘汰最舊的。
#include "core/history.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

namespace tmw::core {
namespace {

using Strings = std::vector<std::string>;

PipelineResult result(const Strings& sources, int lens = 1,
                      Language language = Language::Japanese) {
    PipelineResult out;
    out.lens = lens;
    out.language = language;
    for (const std::string& source : sources) {
        TextBlock block;
        block.text = source;
        out.groups.push_back(TranslatedBlock{block, "譯:" + source});
    }
    return out;
}

std::chrono::system_clock::time_point at(int seconds) {
    return std::chrono::system_clock::time_point{} + std::chrono::seconds(seconds);
}

Strings sourcesOf(const HistoryCard& card) {
    Strings out;
    for (const HistoryGroup& group : card.groups) {
        out.push_back(group.source);
    }
    return out;
}

TEST(HistoryTest, AddsACardWithEveryGroup) {
    History history;
    const auto card = history.add(result({"こんにちは", "さようなら"}), at(5));
    ASSERT_TRUE(card.has_value());
    EXPECT_EQ(sourcesOf(*card), (Strings{"こんにちは", "さようなら"}));
    EXPECT_EQ(card->groups[0].translation, "譯:こんにちは");
    EXPECT_EQ(card->lens, 1);
    EXPECT_EQ(card->language, Language::Japanese);
    EXPECT_EQ(card->time, at(5));
    EXPECT_EQ(history.size(), 1u);
}

TEST(HistoryTest, CardsGetIncreasingIds) {
    History history;
    const auto first = history.add(result({"一つ"}), at(1));
    const auto second = history.add(result({"二つ"}), at(2));
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_LT(first->id, second->id);
}

TEST(HistoryTest, OnlyKeepsWhatIsNew) {
    // 捲動網頁時，畫面上大部分的句子和上一次一樣
    History history;
    history.add(result({"一つ目", "二つ目", "三つ目"}), at(1));
    const auto card = history.add(result({"二つ目", "三つ目", "四つ目"}), at(2));
    ASSERT_TRUE(card.has_value());
    EXPECT_EQ(sourcesOf(*card), (Strings{"四つ目"}));
}

TEST(HistoryTest, NothingNewMeansNoCard) {
    History history;
    history.add(result({"こんにちは"}), at(1));
    EXPECT_FALSE(history.add(result({"こんにちは"}), at(2)).has_value());
    EXPECT_EQ(history.size(), 1u);
}

TEST(HistoryTest, WhitespaceOnlyDifferencesAreNotNew) {
    History history;
    history.add(result({"こんにちは"}), at(1));
    EXPECT_FALSE(history.add(result({"  こんにちは "}), at(2)).has_value());
}

TEST(HistoryTest, ComparesAgainstTheWholePreviousResultNotTheFilteredCard) {
    // 卡片本身已經被過濾過，拿它來比對的話，被濾掉的句子下一次又會變成「新的」
    History history;
    history.add(result({"一つ目", "二つ目"}), at(1));
    history.add(result({"一つ目", "二つ目", "三つ目"}), at(2));  // 卡片只有「三つ目」
    EXPECT_FALSE(history.add(result({"一つ目", "二つ目", "三つ目"}), at(3)).has_value());
}

TEST(HistoryTest, AnEmptyResultAddsNothing) {
    History history;
    EXPECT_FALSE(history.add(result({}), at(1)).has_value());
    EXPECT_EQ(history.size(), 0u);
}

TEST(HistoryTest, LensesAreIndependent) {
    History history;
    history.add(result({"こんにちは"}, 1), at(1));
    const auto other = history.add(result({"こんにちは"}, 2), at(2));
    ASSERT_TRUE(other.has_value()) << "另一個透鏡看到同樣的句子仍然是新的";
    EXPECT_EQ(other->lens, 2);
    EXPECT_EQ(history.size(), 2u);
}

TEST(HistoryTest, ForgettingALensMakesEverythingNewAgain) {
    History history;
    history.add(result({"こんにちは"}), at(1));
    history.forget(1);
    EXPECT_TRUE(history.add(result({"こんにちは"}), at(2)).has_value());
}

TEST(HistoryTest, DropsTheOldestCardsOverTheLimit) {
    History history(3);
    for (int i = 0; i < 5; ++i) {
        history.add(result({"句子" + std::to_string(i)}), at(i));
    }
    EXPECT_EQ(history.size(), 3u);
    EXPECT_EQ(history.cards().front().groups[0].source, "句子2") << "最舊的被淘汰";
    EXPECT_EQ(history.cards().back().groups[0].source, "句子4");
}

TEST(HistoryTest, DefaultCapacityIsFiveHundred) {
    EXPECT_EQ(History{}.capacity(), 500u);
    EXPECT_EQ(History{0}.capacity(), 1u);
}

TEST(HistoryTest, KeepsTheTranslationError) {
    History history;
    PipelineResult failed = result({"こんにちは"});
    failed.error = "連線失敗";
    for (TranslatedBlock& group : failed.groups) {
        group.translation.clear();
    }
    const auto card = history.add(failed, at(1));
    ASSERT_TRUE(card.has_value());
    EXPECT_EQ(card->error, "連線失敗");
    EXPECT_TRUE(card->groups[0].translation.empty());
    EXPECT_EQ(card->groups[0].source, "こんにちは") << "翻譯失敗時原文還是要看得到";
}

TEST(HistoryTest, ClearEmptiesEverything) {
    History history;
    history.add(result({"こんにちは"}), at(1));
    history.clear();
    EXPECT_EQ(history.size(), 0u);
    EXPECT_TRUE(history.add(result({"こんにちは"}), at(2)).has_value()) << "清空後重新開始";
}

}  // namespace
}  // namespace tmw::core
