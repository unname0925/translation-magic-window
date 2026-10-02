#include "ui/engine_choice.h"

#include <algorithm>
#include <array>

namespace tmw::ui {
namespace {

constexpr const char* kGoogleId = "google";
// 設定畫面認得的 LLM 引擎（M2-07）
constexpr std::array<const char*, 2> kLlmIds{"openai-compatible", "anthropic"};

const core::EngineSettings* find(const core::Settings& settings, const std::string& id) {
    const auto found = std::ranges::find_if(
        settings.engines, [&id](const core::EngineSettings& engine) { return engine.id == id; });
    return found == settings.engines.end() ? nullptr : &*found;
}

}  // namespace

EngineChoice engineChoiceFrom(const core::Settings& settings) {
    EngineChoice choice;
    const core::EngineSettings* llm = nullptr;
    for (const core::EngineSettings& engine : settings.engines) {
        if (std::ranges::find(kLlmIds, engine.id) != kLlmIds.end()) {
            llm = &engine;  // 引擎鏈裡第一個 LLM
            break;
        }
    }
    choice.useLlm = llm != nullptr;
    if (llm != nullptr) {
        choice.llmId = llm->id;
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
        llm.id = choice.llmId;
        llm.endpoint = choice.endpoint;
        llm.model = choice.model;
        if (!encryptedKey.empty()) {
            llm.encryptedApiKey = encryptedKey;
        } else if (const core::EngineSettings* existing = find(current, choice.llmId)) {
            // 沒有重填就沿用原本的。只沿用同一種引擎的：換了一家，舊金鑰給新的那家用一定失敗，
            // 還會把金鑰送到別家的伺服器
            llm.encryptedApiKey = existing->encryptedApiKey;
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
