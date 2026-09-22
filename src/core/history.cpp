#include "core/history.h"

#include <algorithm>
#include <utility>

#include "core/translation_cache.h"

namespace tmw::core {

History::History(std::size_t capacity) : capacity_(std::max<std::size_t>(capacity, 1)) {}

void History::forget(int lens) {
    std::erase_if(memories_, [lens](const LensMemory& memory) { return memory.lens == lens; });
}

void History::clear() {
    cards_.clear();
    memories_.clear();
}

std::optional<HistoryCard> History::add(const PipelineResult& result,
                                        std::chrono::system_clock::time_point time) {
    if (result.groups.empty()) {
        return std::nullopt;
    }

    LensMemory* memory = nullptr;
    for (LensMemory& candidate : memories_) {
        if (candidate.lens == result.lens) {
            memory = &candidate;
            break;
        }
    }
    if (memory == nullptr) {
        memories_.push_back(LensMemory{result.lens, {}});
        memory = &memories_.back();
    }

    HistoryCard card;
    card.time = time;
    card.lens = result.lens;
    card.language = result.language;
    card.error = result.error;

    std::vector<std::string> seen;
    seen.reserve(result.groups.size());
    for (const TranslatedBlock& group : result.groups) {
        std::string key = normalizeSource(group.block.text);
        const bool isNew =
            std::find(memory->seen.begin(), memory->seen.end(), key) == memory->seen.end();
        if (isNew) {
            card.groups.push_back(HistoryGroup{group.block.text, group.translation});
        }
        seen.push_back(std::move(key));
    }
    memory->seen = std::move(seen);

    if (card.groups.empty()) {
        return std::nullopt;
    }
    card.id = nextId_++;
    cards_.push_back(std::move(card));
    while (cards_.size() > capacity_) {
        cards_.pop_front();
    }
    return cards_.back();
}

}  // namespace tmw::core
