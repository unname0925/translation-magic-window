#include "core/settings.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace tmw::core {
namespace {

using nlohmann::json;

// 讀一個欄位；缺少或型別不對時保留預設值。設定檔是使用者可以手改的，所以單一欄位壞掉
// 不應該讓整份設定作廢。
template <typename T>
void read(const json& object, const char* key, T& target) {
    if (const auto it = object.find(key); it != object.end()) {
        if (const auto* value = it->template get_ptr<const T*>(); value != nullptr) {
            target = *value;
        }
    }
}

void readNumber(const json& object, const char* key, double& target) {
    if (const auto it = object.find(key); it != object.end() && it->is_number()) {
        target = it->get<double>();
    }
}

void readInt(const json& object, const char* key, int& target) {
    if (const auto it = object.find(key); it != object.end() && it->is_number_integer()) {
        target = it->get<int>();
    }
}

EngineSettings readEngine(const json& object) {
    EngineSettings engine;
    read(object, "id", engine.id);
    read(object, "endpoint", engine.endpoint);
    read(object, "model", engine.model);
    read(object, "encryptedApiKey", engine.encryptedApiKey);
    return engine;
}

ResultWindowSettings readResultWindow(const json& object) {
    ResultWindowSettings window;
    readNumber(object, "fontScale", window.fontScale);
    read(object, "alwaysOnTop", window.alwaysOnTop);
    read(object, "theme", window.theme);
    readInt(object, "fontPoints", window.fontPoints);
    if (const auto it = object.find("geometry"); it != object.end() && it->is_object()) {
        readInt(*it, "left", window.geometry.left);
        readInt(*it, "top", window.geometry.top);
        readInt(*it, "right", window.geometry.right);
        readInt(*it, "bottom", window.geometry.bottom);
        if (window.geometry.empty()) {
            window.geometry = RectI{};  // 壞掉的矩形一律當成「還沒記過」
        }
    }
    window.fontScale = std::clamp(window.fontScale, 0.5, 3.0);
    window.fontPoints = std::clamp(window.fontPoints, 7, 28);
    if (window.theme != "system" && window.theme != "light" && window.theme != "dark") {
        window.theme = ResultWindowSettings{}.theme;
    }
    return window;
}

}  // namespace

std::span<const SettingsMigration> settingsMigrations() {
    // 改變欄位的意義時，在這裡加一個「版本 n → n+1」的函式，版本號會自動加一。
    static const std::vector<SettingsMigration> all;
    return all;
}

SettingsLoad parseSettings(std::string_view json_text,
                           std::span<const SettingsMigration> migrations) {
    const int current = 1 + static_cast<int>(migrations.size());
    SettingsLoad result;
    json document = json::parse(json_text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        result.problem = "設定檔不是有效的 JSON 物件";
        return result;
    }

    int version = current;
    readInt(document, "schemaVersion", version);
    if (version > current) {
        result.problem = "設定檔的版本（" + std::to_string(version) + "）比這個程式新（" +
                         std::to_string(current) + "）";
        return result;
    }
    if (version < 1) {
        result.problem = "設定檔的版本不合理：" + std::to_string(version);
        return result;
    }
    for (int from = version; from < current; ++from) {
        migrations[static_cast<std::size_t>(from) - 1](document);
    }

    Settings settings;
    read(document, "verboseDiagnostics", settings.verboseDiagnostics);
    if (const auto it = document.find("engines"); it != document.end() && it->is_array()) {
        for (const json& entry : *it) {
            if (entry.is_object()) {
                settings.engines.push_back(readEngine(entry));
            }
        }
    }
    if (const auto it = document.find("resultWindow"); it != document.end() && it->is_object()) {
        settings.resultWindow = readResultWindow(*it);
    }
    result.settings = std::move(settings);
    return result;
}

std::string serializeSettings(const Settings& settings, int schemaVersion) {
    json engines = json::array();
    for (const EngineSettings& engine : settings.engines) {
        engines.push_back({{"id", engine.id},
                           {"endpoint", engine.endpoint},
                           {"model", engine.model},
                           {"encryptedApiKey", engine.encryptedApiKey}});
    }
    const json document = {
        {"schemaVersion", schemaVersion},
        {"verboseDiagnostics", settings.verboseDiagnostics},
        {"engines", std::move(engines)},
        {"resultWindow",
         {{"fontScale", settings.resultWindow.fontScale},
          {"alwaysOnTop", settings.resultWindow.alwaysOnTop},
          {"theme", settings.resultWindow.theme},
          {"fontPoints", settings.resultWindow.fontPoints},
          {"geometry",
           {{"left", settings.resultWindow.geometry.left},
            {"top", settings.resultWindow.geometry.top},
            {"right", settings.resultWindow.geometry.right},
            {"bottom", settings.resultWindow.geometry.bottom}}}}},
    };
    return document.dump(2) + "\n";
}

}  // namespace tmw::core
