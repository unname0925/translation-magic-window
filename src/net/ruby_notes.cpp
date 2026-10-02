#include "net/ruby_notes.h"

#include <cstddef>
#include <map>
#include <utility>

#include "core/ruby.h"

namespace tmw::net {

std::vector<std::string> translateWithRubyNotes(
    std::span<const std::string> segments,
    const std::function<std::vector<std::string>(std::span<const std::string>)>& translateAll) {
    std::vector<std::string> toSend;
    toSend.reserve(segments.size());
    std::vector<std::vector<std::pair<std::string, std::string>>> rubyOf(segments.size());
    std::map<std::string, std::size_t> extraIndex;  // 本文或讀音 -> 在 toSend 的位置
    std::vector<std::string> extras;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        toSend.push_back(core::stripRubyMarkup(segments[i]));
        rubyOf[i] = core::rubyMarkupPairs(segments[i]);
        for (const auto& [base, reading] : rubyOf[i]) {
            for (const std::string& word : {base, reading}) {
                if (extraIndex.try_emplace(word, segments.size() + extras.size()).second) {
                    extras.push_back(word);
                }
            }
        }
    }
    toSend.insert(toSend.end(), extras.begin(), extras.end());

    std::vector<std::string> translated = translateAll(toSend);
    translated.resize(toSend.size());

    // 「我要認真打一場　［本気（マジ）→ 認真（玩真的）］」
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (rubyOf[i].empty()) {
            continue;
        }
        std::string note;
        for (const auto& [base, reading] : rubyOf[i]) {
            note += note.empty() ? "" : "、";
            note += base + "（" + reading + "）→ " + translated[extraIndex.at(base)] + "（" +
                    translated[extraIndex.at(reading)] + "）";
        }
        translated[i] += "　［" + note + "］";
    }
    translated.resize(segments.size());
    return translated;
}

}  // namespace tmw::net
