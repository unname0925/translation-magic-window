// 結果視窗的資料來源：卡片的新增、上限和清除。
#include "ui/history_model.h"

#include <gtest/gtest.h>

#include <string>

#include "ui/card_text.h"

namespace tmw::ui {
namespace {

core::HistoryCard card(const std::string& source, int lens = 1) {
    core::HistoryCard out;
    out.lens = lens;
    out.language = core::Language::Japanese;
    out.groups.push_back(core::HistoryGroup{source, "譯:" + source});
    return out;
}

TEST(HistoryModelTest, StartsEmpty) {
    const HistoryModel model;
    EXPECT_EQ(model.rowCount(), 0);
    EXPECT_EQ(model.cardAt(0), nullptr);
}

TEST(HistoryModelTest, AppendsCards) {
    HistoryModel model;
    model.appendCard(card("一つ目"));
    model.appendCard(card("二つ目"));
    ASSERT_EQ(model.rowCount(), 2);
    ASSERT_NE(model.cardAt(1), nullptr);
    EXPECT_EQ(model.cardAt(1)->groups[0].source, "二つ目");
}

TEST(HistoryModelTest, OutOfRangeRowsAreNull) {
    HistoryModel model;
    model.appendCard(card("一つ目"));
    EXPECT_EQ(model.cardAt(-1), nullptr);
    EXPECT_EQ(model.cardAt(1), nullptr);
}

TEST(HistoryModelTest, DropsTheOldestOverTheLimit) {
    HistoryModel model;
    model.setCapacity(2);
    model.appendCard(card("一つ目"));
    model.appendCard(card("二つ目"));
    model.appendCard(card("三つ目"));
    ASSERT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.cardAt(0)->groups[0].source, "二つ目");
}

TEST(HistoryModelTest, LoweringTheCapacityDropsOldCards) {
    HistoryModel model;
    for (int i = 0; i < 5; ++i) {
        model.appendCard(card("句子" + std::to_string(i)));
    }
    model.setCapacity(2);
    ASSERT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.cardAt(0)->groups[0].source, "句子3");
    model.setCapacity(0);
    EXPECT_EQ(model.capacity(), 1u) << "上限至少是 1";
}

TEST(HistoryModelTest, ClearRemovesEverything) {
    HistoryModel model;
    model.appendCard(card("一つ目"));
    model.clear();
    EXPECT_EQ(model.rowCount(), 0);
    model.clear() /* 已經空了，不該出事 */;
    EXPECT_EQ(model.rowCount(), 0);
}

TEST(HistoryModelTest, DisplayRoleHasBothTexts) {
    HistoryModel model;
    model.appendCard(card("こんにちは"));
    const QString text = model.data(model.index(0), Qt::DisplayRole).toString();
    EXPECT_TRUE(text.contains(QStringLiteral("こんにちは")));
    EXPECT_TRUE(text.contains(QStringLiteral("譯:こんにちは")));
}

TEST(CardTextTest, ShowsTimeLensAndLanguage) {
    core::HistoryCard sample = card("こんにちは", 2);
    // 2026-09-22 14:32:05 UTC
    sample.time = std::chrono::system_clock::time_point{} + std::chrono::seconds(1790087525);
    const QString header = cardHeader(sample);
    EXPECT_TRUE(header.contains(QStringLiteral("透鏡 2"))) << header.toStdString();
    EXPECT_TRUE(header.contains(QStringLiteral("日文"))) << header.toStdString();
    EXPECT_TRUE(header.contains(QLatin1Char(':'))) << "要有時間" << header.toStdString();
}

TEST(CardTextTest, NamesEveryLanguage) {
    EXPECT_EQ(languageName(core::Language::Japanese), QStringLiteral("日文"));
    EXPECT_EQ(languageName(core::Language::English), QStringLiteral("英文"));
    EXPECT_EQ(languageName(core::Language::Korean), QStringLiteral("韓文"));
    EXPECT_EQ(languageName(core::Language::Unknown), QStringLiteral("未知語言"));
}

}  // namespace
}  // namespace tmw::ui
