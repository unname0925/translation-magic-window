// 設定檔的讀寫（見 docs/design.md 4.10）。
//
// 位置：%APPDATA%\TranslationMagicWindow\settings.json；用 --data-dir 指定資料夾時
// 改成 <資料夾>\settings.json，這樣測試不會動到使用者的設定。
//
// 讀不懂的檔案（JSON 壞掉、版本比程式新）會先備份成 settings.json.bak，再改用預設值，
// 避免使用者手改壞了就直接失去全部設定。寫檔先寫暫存檔再取代，中途斷電不會留下半個檔案。
#pragma once

#include <filesystem>
#include <optional>

#include "core/settings.h"

namespace tmw::platform {

// 預設的設定檔位置（%APPDATA%\TranslationMagicWindow\settings.json，資料夾不存在時會建立）
std::filesystem::path defaultSettingsPath();

// <資料夾>\settings.json
std::filesystem::path settingsPathIn(const std::filesystem::path& dataDirectory);

struct SettingsFileLoad {
    core::Settings settings;
    // 檔案不存在（第一次啟動）
    bool missing = false;
    // 讀不懂而改用預設值時的說明，並且原檔已經備份到 backup
    std::string problem;
    std::optional<std::filesystem::path> backup;
};

// 讀取設定檔。任何情況都會回傳可以使用的設定。
SettingsFileLoad loadSettings(const std::filesystem::path& path);

// 寫出設定檔（先寫 .tmp 再取代）。
void saveSettings(const std::filesystem::path& path, const core::Settings& settings);

}  // namespace tmw::platform
