#include "ui/engine_choice.h"

#include <algorithm>

namespace tmw::ui {
namespace {

constexpr const char* kLlmId = "openai-compatible";
constexpr const char* kGoogleId = "google";

const core::EngineSettings* find(const core::Settings& settings, const std::string& id) {
    const auto found = std::ranges::find_if(
        settings.engines, [&id](const core::EngineSettings& engine) { return engine.id == id; });
    return found == settings.engines.end() ? nullptr : &*found;
}

}  // namespace

EngineChoice engineChoiceFrom(const core::Settings& settings) {
    EngineChoice choice;
    const core::EngineSettings* llm = find(settings, kLlmId);
    choice.useLlm = llm != nullptr;
    if (llm != nullptr) {
        choice.endpoint = llm->endpoint;
        choice.model = llm->model;
        choice.hasKey = !llm->encryptedApiKey.empty();
    }
    // 只有一個 Google 的時候，「退回 Google」對使用者沒有意義，維持預設的勾選
    choice.fallbackToGoogle = !choice.useLlm || find(settings, kGoogleId) != nullptr;
    return choice;
}

std::vector<core::EngineSettings> enginesFor(const EngineChoice& choice,
                                             const core::Settings& current,
                                             const std::string& encryptedKey) {
    std::vector<core::EngineSettings> engines;
    if (choice.useLlm) {
        core::EngineSettings llm;
        llm.id = kLlmId;
        llm.endpoint = choice.endpoint;
        llm.model = choice.model;
        if (!encryptedKey.empty()) {
            llm.encryptedApiKey = encryptedKey;
        } else if (const core::EngineSettings* existing = find(current, kLlmId)) {
            llm.encryptedApiKey = existing->encryptedApiKey;  // 沒有重填就沿用原本的
        }
        engines.push_back(std::move(llm));
    }
    if (!choice.useLlm || choice.fallbackToGoogle) {
        core::EngineSettings google;
        google.id = kGoogleId;
        if (const core::EngineSettings* existing = find(current, kGoogleId)) {
            google.endpoint = existing->endpoint;
        }
        engines.push_back(std::move(google));
    }
    return engines;
}

}  // namespace tmw::ui
