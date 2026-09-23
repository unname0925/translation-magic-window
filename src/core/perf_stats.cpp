#include "core/perf_stats.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace tmw::core {
namespace {

// 已排序的樣本中第 fraction 分位。線性內插，樣本少的時候也不會跳來跳去。
double quantile(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) {
        return 0.0;
    }
    if (sorted.size() == 1) {
        return sorted.front();
    }
    const double position = fraction * static_cast<double>(sorted.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    const double weight = position - static_cast<double>(lower);
    return sorted[lower] * (1.0 - weight) + sorted[upper] * weight;
}

std::string oneDecimal(double value) {
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(1);
    text << value;
    return text.str();
}

}  // namespace

std::vector<PerfBudget> defaultPerfBudgets() {
    // design.md 第 5 節「處理延遲預算」（RTX 4070）
    return {{"OCR", 150.0}, {"分段", 20.0}, {"翻譯", 1000.0}, {"合計", 2000.0}};
}

PerfSummary summarize(std::vector<double> samples) {
    PerfSummary summary;
    summary.count = samples.size();
    if (samples.empty()) {
        return summary;
    }
    std::ranges::sort(samples);
    summary.min = samples.front();
    summary.max = samples.back();
    summary.median = quantile(samples, 0.5);
    summary.p95 = quantile(samples, 0.95);
    return summary;
}

void PerfStats::push(std::vector<double>& samples, double value) {
    if (capacity_ == 0) {
        return;
    }
    if (samples.size() >= capacity_) {
        samples.erase(samples.begin());
    }
    samples.push_back(value);
}

void PerfStats::add(const PipelineTimings& timings) {
    push(ocr_, timings.ocrMs);
    push(layout_, timings.layoutMs);
    push(translation_, timings.translationMs);
    push(total_, timings.totalMs());
}

void PerfStats::clear() {
    ocr_.clear();
    layout_.clear();
    translation_.clear();
    total_.clear();
}

PerfSummary PerfStats::ocr() const {
    return summarize(ocr_);
}
PerfSummary PerfStats::layout() const {
    return summarize(layout_);
}
PerfSummary PerfStats::translation() const {
    return summarize(translation_);
}
PerfSummary PerfStats::total() const {
    return summarize(total_);
}

std::string PerfStats::report(const std::vector<PerfBudget>& budgets) const {
    const std::vector<std::pair<std::string, PerfSummary>> rows{
        {"OCR", ocr()}, {"分段", layout()}, {"翻譯", translation()}, {"合計", total()}};

    std::ostringstream text;
    text << "共 " << count() << " 次\n";
    text << "步驟      最短    中位數     p95    最長    預算\n";
    for (const auto& [name, summary] : rows) {
        double budget = 0.0;
        for (const PerfBudget& one : budgets) {
            if (one.step == name) {
                budget = one.budgetMs;
            }
        }
        text << name;
        // 中文字寬度不一，補到固定寬度只是為了看起來整齊
        for (std::size_t i = name.size() / 3; i < 4; ++i) {
            text << "  ";
        }
        text << oneDecimal(summary.min) << "\t" << oneDecimal(summary.median) << "\t"
             << oneDecimal(summary.p95) << "\t" << oneDecimal(summary.max) << "\t";
        if (budget <= 0.0) {
            text << "－";
        } else {
            text << oneDecimal(budget);
            // 看的是 p95：偶爾卡一下才是使用者會抱怨的地方
            if (summary.count > 0 && summary.p95 > budget) {
                text << "  ← 超出預算";
            }
        }
        text << "\n";
    }
    return text.str();
}

}  // namespace tmw::core
