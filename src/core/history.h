// 歷史紀錄：結果視窗裡一張一張的卡片（見 docs/design.md 4.7）。
//
// 只保存在記憶體中，關閉程式就清除，也不做匯出。
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "core/language.h"
#include "core/pipeline.h"

namespace tmw::core {

// 一組 = 一個文字區塊
struct HistoryGroup {
    std::string source;
    std::string translation;

    friend bool operator==(const HistoryGroup&, const HistoryGroup&) = default;
};

// 一張卡片 = 同一次翻譯產生的所有組
struct HistoryCard {
    std::uint64_t id = 0;  // 遞增，UI 用來辨識卡片
    std::chrono::system_clock::time_point time;
    int lens = 0;
    Language language = Language::Unknown;
    std::vector<HistoryGroup> groups;
    std::string error;  // 翻譯失敗的原因（原文仍然看得到）
};

class History {
public:
    static constexpr std::size_t kDefaultCapacity = 500;

    explicit History(std::size_t capacity = kDefaultCapacity);

    // 加入一次翻譯結果。只留下「和同一個透鏡上一次的結果相比，新出現的區塊」，
    // 沒有新內容時不新增卡片並回傳 nullopt（捲動網頁時才不會一直重複記錄同樣的句子）。
    //
    // 和 design.md 4.7 的字面說法不同：這裡比對的是「上一次的結果」而不是「上一張卡片」。
    // 卡片本身已經被過濾過，拿它來比對的話，被濾掉的句子下一次又會變成「新的」。
    std::optional<HistoryCard> add(const PipelineResult& result,
                                   std::chrono::system_clock::time_point time);

    // 透鏡被拖到別的地方：接下來看到的都算新的
    void forget(int lens);

    const std::deque<HistoryCard>& cards() const { return cards_; }
    std::size_t size() const { return cards_.size(); }
    std::size_t capacity() const { return capacity_; }
    void clear();

private:
    struct LensMemory {
        int lens = 0;
        std::vector<std::string> seen;  // 上一次結果中每一段正規化後的原文
    };

    std::size_t capacity_;
    std::uint64_t nextId_ = 1;
    std::deque<HistoryCard> cards_;
    std::vector<LensMemory> memories_;
};

}  // namespace tmw::core
