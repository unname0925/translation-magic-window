#include "core/engine_history.h"

#include <algorithm>

namespace tmw::core {
namespace {

bool sameEngine(const EngineSettings& a, const EngineSettings& b) {
    return a.id == b.id && a.endpoint == b.endpoint && a.model == b.model && a.region == b.region;
}

// 網址只留主機和埠：「http://127.0.0.1:11434/v1」→「127.0.0.1:11434」
std::string hostOf(const std::string& url) {
    std::string host = url;
    if (const std::size_t scheme = host.find("://"); scheme != std::string::npos) {
        host = host.substr(scheme + 3);
    }
    if (const std::size_t path = host.find('/'); path != std::string::npos) {
        host = host.substr(0, path);
    }
    return host;
}

std::string kindName(const EngineSettings& engine) {
    if (engine.id == "openai-compatible") {
        return engine.endpoint.find(":11434") != std::string::npos ? "Ollama" : "OpenAI 相容";
    }
    if (engine.id == "anthropic") {
        return "Claude";
    }
    if (engine.id == "deepl") {
        return "DeepL";
    }
    if (engine.id == "azure") {
        return "Microsoft Translator";
    }
    if (engine.id == "google-cloud") {
        return "Google Cloud";
    }
    if (engine.id == "custom-http") {
        return "自訂 HTTP";
    }
    return engine.id;
}

}  // namespace

void rememberCurrentEngine(Settings& settings) {
    if (settings.engines.empty()) {
        return;
    }
    const EngineSettings current = settings.engines.front();
    std::erase_if(settings.engineHistory,
                  [&current](const EngineSettings& old) { return sameEngine(old, current); });
    settings.engineHistory.insert(settings.engineHistory.begin(), current);
    if (settings.engineHistory.size() > kEngineHistoryLimit) {
        settings.engineHistory.resize(kEngineHistoryLimit);
    }
}

std::string engineLabel(const EngineSettings& engine) {
    if (engine.id == "google") {
        return "Google 翻譯（免費）";
    }
    std::string where = kindName(engine);
    if (const std::string host = hostOf(engine.endpoint); !host.empty()) {
        where += " " + host;
    }
    if (!engine.region.empty()) {
        where += " " + engine.region;
    }
    return engine.model.empty() ? where : engine.model + "（" + where + "）";
}

bool useEngineFromHistory(Settings& settings, std::size_t index) {
    if (index >= settings.engineHistory.size()) {
        return false;
    }
    const EngineSettings chosen = settings.engineHistory[index];
    const bool fallback =
        settings.engines.size() > 1 &&
        std::any_of(settings.engines.begin() + 1, settings.engines.end(),
                    [](const EngineSettings& engine) { return engine.id == "google"; });
    std::vector<EngineSettings> chain{chosen};
    if (chosen.id != "google" && fallback) {
        EngineSettings google;
        google.id = "google";
        // 原本那一筆 Google 的設定（通常是空的）照樣沿用
        for (const EngineSettings& engine : settings.engines) {
            if (engine.id == "google") {
                google = engine;
            }
        }
        chain.push_back(google);
    }
    settings.engines = std::move(chain);
    rememberCurrentEngine(settings);
    return true;
}

}  // namespace tmw::core
