// 把 OCR 的逐行結果整理成「一段一段」的文字區塊（見 docs/design.md 4.4）。
//
// 一個區塊就是結果視窗中的「一組」：一個對話框、一段段落。整理的步驟是
//   排出閱讀順序 → 依距離和對齊把相鄰的行合併 → 依語言把換行接起來。
//
// 這裡是純邏輯，不依賴 OpenCV 或 Windows：OCR 的結果由呼叫端轉成 OcrLine。
#pragma once

#include <span>
#include <string>
#include <vector>

#include "core/geometry.h"
#include "core/language.h"

namespace tmw::core {

enum class Orientation {
    Horizontal,
    Vertical,
};

// OCR 的一行
struct OcrLine {
    RectI rect;          // 畫面座標
    std::string text;    // UTF-8
    float score = 0.0f;  // 辨識分數
    Orientation orientation = Orientation::Horizontal;

    friend bool operator==(const OcrLine&, const OcrLine&) = default;
};

// 合併後的一段
struct TextBlock {
    std::string text;  // 已經照語言接好換行
    RectI rect;        // 所有行的外框
    Orientation orientation = Orientation::Horizontal;
    Language language = Language::Unknown;
    float score = 0.0f;  // 各行分數的平均
    std::vector<OcrLine> lines;

    friend bool operator==(const TextBlock&, const TextBlock&) = default;
};

struct MergeOptions {
    // 行距不超過「字高 × 這個倍數」才算同一段
    double lineGapRatio = 0.8;
    // 兩行的起點或終點要對齊到「字高 × 這個倍數」以內
    double alignRatio = 0.6;
    // 字高相差超過這個倍數就不是同一段（標題和內文不該合併）
    double heightRatio = 1.5;
    // 橫排的行還要左右重疊到這個比例才算同一段（避免把並排的兩欄接在一起）
    double overlapRatio = 0.3;
};

// 依閱讀順序排序：橫排由上到下、由左到右；直排（日文漫畫）由右到左、由上到下。
// 同一行（或同一欄）的判定會用字高當作容忍值。
void sortReadingOrder(std::vector<OcrLine>& lines);

// 把相鄰的行合併成段落。lines 不需要事先排序。
std::vector<TextBlock> mergeIntoBlocks(std::span<const OcrLine> lines,
                                       const MergeOptions& options = {});

// 依語言把多行接成一段文字：
// - 英文、韓文：用空格接；英文行尾的連字號（trans- / lation）要接回同一個字
// - 日文：直接相連
std::string joinLines(std::span<const std::string> lines, Language language);

// 區塊有沒有碰到畫面邊緣（被切掉一部分）。透鏡邊緣的殘缺句子預設不翻譯（design.md 4.4）。
bool touchesEdge(const RectI& rect, const SizeI& frame, int margin = 2);

// 去掉碰到邊緣的區塊
std::vector<TextBlock> dropEdgeBlocks(std::span<const TextBlock> blocks, const SizeI& frame,
                                      int margin = 2);

}  // namespace tmw::core
