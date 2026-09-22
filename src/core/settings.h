// 設定檔的內容和解析（見 docs/design.md 4.11）。
//
// 檔案是 JSON，帶有 schemaVersion：
// - 版本比目前舊：依序套用遷移函式。
// - 版本比目前新，或 JSON 壞掉：視為無法使用，交給呼叫端備份原檔並改用預設值。
// - 欄位缺少或型別不對：用預設值補上，其餘欄位照常讀取。
#pragma once

#include <functional>
#include <nlohmann/json_fwd.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tmw::core {

// 一個翻譯引擎的設定。金鑰以加密後的形式存放（見 platform/secret.h），這裡只當成字串搬運。
struct EngineSettings {
    std::string id;               // 例如 "google"、"openai-compatible"
    std::string endpoint;         // 自訂網址（空字串表示用預設）
    std::string model;            // LLM 的模型名稱
    std::string encryptedApiKey;  // DPAPI 加密後再轉成 base64；記錄檔和除錯傾印都不會包含

    friend bool operator==(const EngineSettings&, const EngineSettings&) = default;
};

struct ResultWindowSettings {
    double fontScale = 1.0;  // 0.5～3.0
    bool alwaysOnTop = false;
    std::string theme = "system";  // "system"、"light"、"dark"

    friend bool operator==(const ResultWindowSettings&, const ResultWindowSettings&) = default;
};

struct Settings {
    // 詳細診斷：打開後才會把擷取到的文字和譯文寫進記錄檔（預設關閉，見 design.md 4.12）
    bool verboseDiagnostics = false;
    // 翻譯引擎的順序就是引擎鏈的順序（design.md 4.5）
    std::vector<EngineSettings> engines;
    ResultWindowSettings resultWindow;

    friend bool operator==(const Settings&, const Settings&) = default;
};

// 把設定檔從某個版本改成下一個版本。清單中的第 n 個函式負責「版本 n+1 → n+2」。
using SettingsMigration = std::function<void(nlohmann::json&)>;

// 內建的遷移函式。目前是空的：設定檔還在第一版。
std::span<const SettingsMigration> settingsMigrations();

// 設定檔目前的版本。加了遷移函式就會自動加一。
inline int settingsSchemaVersion() {
    return 1 + static_cast<int>(settingsMigrations().size());
}

// parseSettings 的結果。settings 一定可以使用：讀不懂的時候就是預設值。
struct SettingsLoad {
    Settings settings;
    // 空字串表示順利讀取；否則是「為什麼改用預設值」的說明（呼叫端會備份原檔並記錄）
    std::string problem;

    bool usedDefaults() const { return !problem.empty(); }
};

// migrations 只有測試會指定，正式程式用 settingsMigrations()。
SettingsLoad parseSettings(std::string_view json,
                           std::span<const SettingsMigration> migrations = settingsMigrations());

// 寫出目前版本的設定檔（最後有換行，方便人看和用版本控制比對）
std::string serializeSettings(const Settings& settings,
                              int schemaVersion = settingsSchemaVersion());

}  // namespace tmw::core
