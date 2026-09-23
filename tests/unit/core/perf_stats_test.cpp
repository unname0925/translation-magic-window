// 各步驟耗時的統計（M1-15）。
#include "core/perf_stats.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace tmw::core {
namespace {

PipelineTimings timings(double ocr, double layout = 1.0, double translation = 10.0) {
    return {.ocrMs = ocr, .layoutMs = layout, .translationMs = translation};
}

TEST(SummarizeTest, NoSamplesIsAllZero) {
    const PerfSummary summary = summarize({});
    EXPECT_EQ(summary.count, 0u);
    EXPECT_EQ(summary.median, 0.0);
    EXPECT_EQ(summary.p95, 0.0);
}

TEST(SummarizeTest, OneSampleIsItsOwnEverything) {
    const PerfSummary summary = summarize({42.0});
    EXPECT_EQ(summary.count, 1u);
    EXPECT_EQ(summary.min, 42.0);
    EXPECT_EQ(summary.median, 42.0);
    EXPECT_EQ(summary.p95, 42.0);
    EXPECT_EQ(summary.max, 42.0);
}

TEST(SummarizeTest, DoesNotCareAboutTheOrder) {
    const PerfSummary summary = summarize({5.0, 1.0, 3.0});
    EXPECT_EQ(summary.min, 1.0);
    EXPECT_EQ(summary.median, 3.0);
    EXPECT_EQ(summary.max, 5.0);
}

TEST(SummarizeTest, TheMedianIsBetweenTheTwoMiddleOnes) {
    EXPECT_DOUBLE_EQ(summarize({10.0, 20.0, 30.0, 40.0}).median, 25.0);
}

TEST(SummarizeTest, P95IgnoresAllButTheWorst) {
    // 100 次裡有 5 次很慢：p95 要看得到它們，中位數不該被拉走
    std::vector<double> samples(95, 10.0);
    samples.insert(samples.end(), 5, 1000.0);
    const PerfSummary summary = summarize(samples);
    EXPECT_DOUBLE_EQ(summary.median, 10.0);
    EXPECT_GT(summary.p95, 10.0);
    EXPECT_EQ(summary.max, 1000.0);
}

TEST(PerfStatsTest, SplitsTheStepsApart) {
    PerfStats stats;
    stats.add(timings(100.0, 2.0, 500.0));
    stats.add(timings(200.0, 4.0, 700.0));

    EXPECT_EQ(stats.count(), 2u);
    EXPECT_DOUBLE_EQ(stats.ocr().median, 150.0);
    EXPECT_DOUBLE_EQ(stats.layout().median, 3.0);
    EXPECT_DOUBLE_EQ(stats.translation().median, 600.0);
    EXPECT_DOUBLE_EQ(stats.total().median, 753.0) << "合計是三個步驟加起來";
}

TEST(PerfStatsTest, KeepsOnlyTheMostRecentOnes) {
    // 程式開著一整天也不該讓樣本一直長
    PerfStats stats(3);
    for (double value : {1.0, 2.0, 3.0, 4.0, 5.0}) {
        stats.add(timings(value));
    }
    EXPECT_EQ(stats.count(), 3u);
    EXPECT_EQ(stats.ocr().min, 3.0) << "留下來的要是最後三次";
    EXPECT_EQ(stats.ocr().max, 5.0);
}

TEST(PerfStatsTest, ClearForgetsEverything) {
    PerfStats stats;
    stats.add(timings(100.0));
    stats.clear();
    EXPECT_EQ(stats.count(), 0u);
}

TEST(PerfStatsReportTest, SaysWhenAStepIsOverBudget) {
    PerfStats stats;
    // OCR 的預算是 150 ms
    for (int i = 0; i < 10; ++i) {
        stats.add(timings(400.0));
    }
    const std::string report = stats.report();
    EXPECT_NE(report.find("超出預算"), std::string::npos);
    EXPECT_NE(report.find("OCR"), std::string::npos);
    EXPECT_NE(report.find("共 10 次"), std::string::npos);
}

TEST(PerfStatsReportTest, SaysNothingAboutBudgetWhenItIsMet) {
    PerfStats stats;
    for (int i = 0; i < 10; ++i) {
        stats.add(timings(50.0, 1.0, 100.0));
    }
    EXPECT_EQ(stats.report().find("超出預算"), std::string::npos);
}

TEST(PerfStatsReportTest, JudgesByP95NotTheAverage) {
    // 19 次很快、1 次很慢：平均看起來沒事，但使用者感覺得到那一次
    PerfStats stats;
    for (int i = 0; i < 19; ++i) {
        stats.add(timings(50.0));
    }
    stats.add(timings(5000.0));
    EXPECT_NE(stats.report().find("超出預算"), std::string::npos);
}

TEST(PerfStatsReportTest, WorksWithNothingMeasuredYet) {
    EXPECT_EQ(PerfStats{}.report().find("超出預算"), std::string::npos);
    EXPECT_NE(PerfStats{}.report().find("共 0 次"), std::string::npos);
}

TEST(PerfBudgetTest, CoversEveryStepInTheReport) {
    const std::vector<PerfBudget> budgets = defaultPerfBudgets();
    for (const char* step : {"OCR", "分段", "翻譯", "合計"}) {
        bool found = false;
        for (const PerfBudget& budget : budgets) {
            found = found || budget.step == step;
        }
        EXPECT_TRUE(found) << step << " 沒有預算";
    }
}

}  // namespace
}  // namespace tmw::core
