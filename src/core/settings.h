// 設定檔的內容和解析（見 docs/design.md 4.11）。
//
// 檔案是 JSON，帶有 schemaVersion：
// - 版本比目前舊：依序套用遷移函式。
// - 版本比目前新，或 JSON 壞掉：視為無法使用，交給呼叫端備份原檔並改用預設值。
// - 欄位缺少或型別不對：用預設值補上，其餘欄位照常讀取。
#pragma once

#include <functional>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/context_profile.h"
#include "core/geometry.h"

namespace tmw::core {

// 一個翻譯引擎的設定。金鑰以加密後的形式存放（見 platform/secret.h），這裡只當成字串搬運。
struct EngineSettings {
    std::string id;               // 例如 "google"、"openai-compatible"
    std::string endpoint;         // 自訂網址（空字串表示用預設）
    std::string model;            // LLM 的模型名稱
    std::string encryptedApiKey;  // DPAPI 加密後再轉成 base64；記錄檔和除錯傾印都不會包含

    friend bool operator==(const EngineSettings&, const EngineSettings&) = default;
};

// 全域快捷鍵（M2-10，寫法見 core/hotkey.h）。空字串代表不使用那個功能的快捷鍵。
struct HotkeySettings {
    std::string translate = "Ctrl+Alt+Shift+T";  // 立即翻譯
    std::string debugDump = "Ctrl+Alt+Shift+D";  // 除錯傾印
    std::string capture = "Ctrl+Alt+Shift+S";    // 把透鏡範圍存成 PNG（開發用）

    friend bool operator==(const HotkeySettings&, const HotkeySettings&) = default;
};

struct ResultWindowSettings {
    double fontScale = 1.0;  // 0.5～3.0
    bool alwaysOnTop = false;
    std::string theme = "system";  // "system"、"light"、"dark"
    int fontPoints = 10;           // 字級（點）
    // 上次的位置和大小。空的代表還沒記過，由視窗自己決定。
    RectI geometry;

    friend bool operator==(const ResultWindowSettings&, const ResultWindowSettings&) = default;
};

struct Settings {
    // 詳細診斷：打開後才會把擷取到的文字和譯文寫進記錄檔（預設關閉，見 design.md 4.12）
    bool verboseDiagnostics = false;
    // 漫畫模式：另外跑 comic-text-detector，同一個對話框裡的行就是同一段（M2-02）。
    // 每次多 37 ms，而且在網頁、遊戲上沒有幫助、還可能把整塊介面框在一起，所以預設關閉。
    bool mangaMode = false;
    // 遊戲模式：第一次辨識之後只看文字區域有沒有變（M2-06，design.md 4.3）。
    // 閃爍的游標、角色動畫不會再讓自動翻譯永遠等不到畫面穩定。
    bool gameMode = false;
    // 辨識語言："auto"（自動判斷）、"ja"、"en"、"ko"。指定之後只用那個語言的模型，
    // 不再判斷（design.md 4.4「語言判斷」第 5 步）。讀不懂的值一律當成 "auto"。
    std::string ocrLanguage = "auto";
    // 碰到透鏡邊緣、被切掉一部分的句子不翻（design.md 4.4）。遊戲情境預設關掉。
    bool dropEdgeBlocks = true;
    // 畫面停下來多久才處理（毫秒，100～3000，design.md 4.3）
    int settleMs = 400;
    // 情境模式（M2-06，core/context_profile.h）：現在選的情境（空字串是沒選），
    // 以及每個情境上一次的設定。上面那幾個欄位一直是現在生效的值。
    std::string profile;
    std::map<std::string, ProfileValues> profiles;
    // 翻譯引擎的順序就是引擎鏈的順序（design.md 4.5）
    std::vector<EngineSettings> engines;
    ResultWindowSettings resultWindow;
    HotkeySettings hotkeys;

    friend bool operator==(const Settings&, const Settings&) = default;
};

// 現在生效的那幾個值（存回情境用）
ProfileValues currentProfileValues(const Settings& settings);
// 切換情境：現在的值存回原本的情境，再載入新情境上一次的值（第一次用時是內建的預設值）。
// id 是空字串代表不使用情境：現在的值維持不變。
void switchProfile(Settings& settings, std::string_view id);

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
