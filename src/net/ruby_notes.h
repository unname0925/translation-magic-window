// 看不懂 `{本文|讀音}` 標記的翻譯引擎（Google、DeepL、Microsoft、Google Cloud）共用的做法
// （design.md 4.5「振り仮名的翻譯」，M2-14）。
//
// 標記會被這些引擎翻掉或弄壞，所以送出前還原成只有本文。但標的是作者刻意的特殊讀音
// （core::markSpecialRuby），拿掉就失去作者想表達的意思：本文和讀音各自當成一段，
// 跟著同一批送出去翻，再以「譯文　［本気（マジ）→ 認真（玩真的）］」的形式附在那一段後面。
#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace tmw::net {

// translateAll 收到「去掉標記的每一段 + 需要另外翻的本文和讀音」，要回傳一樣多的譯文。
// 回傳和 segments 等長的譯文，有特殊讀音的段落後面附上說明。
std::vector<std::string> translateWithRubyNotes(
    std::span<const std::string> segments,
    const std::function<std::vector<std::string>(std::span<const std::string>)>& translateAll);

}  // namespace tmw::net
