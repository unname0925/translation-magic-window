// 各步驟耗時的統計（見 docs/design.md 第 5 節「處理延遲預算」、M1-15）。
//
// 每次處理完就把 PipelineTimings 丟進來，隨時可以問「OCR 的中位數是多少、有沒有超出預算」。
// 平均值會被少數幾次很慢的處理拉走，所以看的是中位數和 p95：
// p95 才代表「偶爾卡一下」有多嚴重，而那正是使用者會抱怨的地方。
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/pipeline.h"

namespace tmw::core {

struct PerfSummary {
    std::size_t count = 0;
    double min = 0.0;
    double median = 0.0;
    double p95 = 0.0;
    double max = 0.0;
};

// design.md 第 5 節的預算。超過 budgetMs 就在報告裡標出來。
struct PerfBudget {
    std::string step;
    double budgetMs = 0.0;
};

// 目前的預算（以 RTX 4070 為準）
std::vector<PerfBudget> defaultPerfBudgets();

class PerfStats {
public:
    // 只留最近這麼多次，長時間執行時記憶體不會一直長
    static constexpr std::size_t kDefaultCapacity = 200;

    explicit PerfStats(std::size_t capacity = kDefaultCapacity) : capacity_(capacity) {}

    void add(const PipelineTimings& timings);
    void clear();

    std::size_t count() const { return ocr_.size(); }

    PerfSummary ocr() const;
    PerfSummary layout() const;
    PerfSummary translation() const;
    PerfSummary total() const;

    // 人看的報告，一行一個步驟，並對照預算標出超過的項目
    std::string report(const std::vector<PerfBudget>& budgets = defaultPerfBudgets()) const;

private:
    void push(std::vector<double>& samples, double value);

    std::size_t capacity_;
    std::vector<double> ocr_;
    std::vector<double> layout_;
    std::vector<double> translation_;
    std::vector<double> total_;
};

// 一組樣本的統計。samples 可以是任何順序，函式自己會排序副本。
PerfSummary summarize(std::vector<double> samples);

}  // namespace tmw::core
