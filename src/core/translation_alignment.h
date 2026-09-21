// 譯文和原文的對齊（見 docs/design.md 4.5 步驟 4）。
//
// 引擎一次送出多段可以省時間也省錢，但回來的東西不一定對得上：LLM 可能多一段、少一段、
// 在 JSON 前後加說明文字、用 ``` 包起來，或是輸出壞掉的 JSON。這裡負責
//   從回應中取出陣列 → 檢查數量和 ルビ 標記 → 對不上時逐段重送。
#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/translator.h"

namespace tmw::core {

// 從 LLM 的回應中取出字串陣列。可以處理：前後夾雜說明文字、用 ``` 或 ```json 包起來、
// 元素是數字（例如原文是 "110" 時 LLM 可能回傳 110）。
// 找不到陣列、JSON 壞掉、元素是物件或巢狀陣列時回傳 nullopt。
std::optional<std::vector<std::string>> parseJsonArray(std::string_view reply);

// 用換行把合併送出的譯文切回來（Google 非官方端點的做法）。不會去掉空行，
// 因為空行代表原文那一段翻出來是空的，數量必須對得上。
std::vector<std::string> splitLines(std::string_view reply);

// 從還沒收完的 JSON 陣列中取出「已經完整」的字串元素。
// 串流時用來邊收邊顯示（design.md 4.6「邊翻邊顯示」）：收到第一段就先填進結果視窗，
// 不必等整個陣列。還在傳輸中的那一段不會回傳。
std::vector<std::string> parseJsonArrayPrefix(std::string_view partial);

// 有另外含義的 ルビ 用 {本文|讀音} 標出來（design.md 4.5），譯文要保留同樣數量的標記。
int countRubyMarkers(std::string_view text);

enum class AlignmentProblem {
    None,
    WrongCount,    // 數量和原文不同
    EmptyText,     // 原文不是空的，譯文卻是空的
    RubyMismatch,  // {本文|讀音} 的數量和原文不同
};

std::string describeAlignmentProblem(AlignmentProblem problem);

AlignmentProblem checkAlignment(std::span<const std::string> sources,
                                std::span<const std::string> translations);

// 送出一批原文並取回等長譯文的動作。失敗時丟出 TranslatorError。
using BatchTranslate = std::function<std::vector<std::string>(std::span<const std::string>)>;

struct AlignOptions {
    // 整批送出的嘗試次數。對不上就再試一次（LLM 有隨機性，重試常常就好了）。
    int batchAttempts = 2;
};

// 整批送出 → 對不上就重試 → 還是不行就逐段重送。
// 只有「回應格式錯誤」才重試：網路錯誤、額度用完、取消都直接往外丟，交給引擎鏈換下一個引擎，
// 才不會在同一個壞掉的引擎上浪費時間和金錢。
std::vector<std::string> translateAligned(std::span<const std::string> sources,
                                          const BatchTranslate& batch,
                                          const AlignOptions& options = {});

}  // namespace tmw::core
