// ルビ的讀音是一般的振り仮名，還是作者刻意的特殊讀音（M2-13，design.md 4.4、4.5）。
//
// 讀音表是 tools/eval/furigana_dict.py build 從 JMdict、KANJIDIC2 產生的（CC BY-SA 4.0）：
// 每個漢字的讀音，加上 JMdict 裡「用每個字的讀音拼不出來」的正規讀法（今日＝きょう）。
// 判斷規則和那支 Python 一模一樣（tools/eval/evaluate_furigana.py 量過準確度）：
// 1. 拼得出來（考慮連濁、促音、「々」、夾在中間的假名）→ 一般讀音
// 2. 是表上的正規讀法 → 一般讀音
// 3. 其餘 → 特殊讀音
// 有不認得的漢字時無法判斷（nullopt），呼叫端退回「讀音是片假名」的舊規則。
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace tmw::core {

class FuriganaReadings {
public:
    // readings.tsv 的內容：「K 字<TAB>讀音,讀音」「W 詞<TAB>讀音」，# 開頭是註解。
    // 讀不到任何漢字時回傳 nullopt。
    static std::optional<FuriganaReadings> parse(std::string_view text);

    // true：特殊讀音；false：一般讀音；nullopt：有不認得的漢字，判斷不了
    std::optional<bool> isSpecial(std::string_view base, std::string_view reading) const;

    std::size_t kanjiCount() const { return kanji_.size(); }

private:
    // 漢字（一個字的 UTF-8）→ 讀音（平假名）
    std::map<std::string, std::set<std::string>, std::less<>> kanji_;
    // 拼不出來的正規讀法：（詞, 讀音）
    std::set<std::pair<std::string, std::string>, std::less<>> irregular_;
};

// 片假名轉平假名（其他字不變）
std::string toHiragana(std::string_view text);

}  // namespace tmw::core
