// 全域快捷鍵的文字格式（M2-10，design.md 4.10）。
//
// 設定檔和設定視窗都用「Ctrl+Alt+Shift+T」這種寫法：修飾鍵的順序不拘、大小寫不拘，
// 正規化之後一律是 Ctrl、Alt、Shift、Win 的順序，按鍵大寫。
// 這裡只處理文字，轉成 Win32 的 RegisterHotKey 參數在 app 層。
#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace tmw::core {

struct Hotkey {
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool win = false;
    // A～Z、0～9、F1～F24
    std::string key;

    friend bool operator==(const Hotkey&, const Hotkey&) = default;
};

// 「ctrl + shift + t」「Shift+Ctrl+T」都可以；Meta 視為 Win（Qt 的寫法）。
// 看不懂的按鍵，或沒有修飾鍵的一般按鍵（單按 T 會讓那個鍵在別的程式裡不能打字）回傳 nullopt。
// F1～F24 可以單獨使用。
std::optional<Hotkey> parseHotkey(std::string_view text);

// 正規化的寫法：「Ctrl+Alt+Shift+T」
std::string formatHotkey(const Hotkey& hotkey);

// 一組快捷鍵設定有沒有問題，給設定視窗在存檔前檢查。沒問題回傳空字串。
// names 和 texts 一一對應（「立即翻譯」對「Ctrl+Alt+Shift+T」）；空字串代表不使用那個功能。
std::string hotkeyProblem(std::span<const std::string> names, std::span<const std::string> texts);

}  // namespace tmw::core
