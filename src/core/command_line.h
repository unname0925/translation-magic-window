#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

namespace tmw::core {

// 命令列參數（見 docs/design.md 4.11）。
struct CommandLineOptions {
    // --data-dir <資料夾>：程式自己寫出的檔案（擷取的 PNG，之後還有設定和記錄檔）
    // 改放到這個資料夾。主要給自動化測試用，讓測試不會動到使用者的資料。
    std::optional<std::filesystem::path> dataDirectory;
    // --ocr-device cpu|dml|auto：強迫 OCR 用哪個裝置。預設 auto（先試 DirectML）。
    // 手動測試矩陣裡的「用 CPU 模式執行，模擬沒有顯示卡的電腦」靠它。
    std::optional<std::string> ocrDevice;
};

class CommandLineError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

// args 不含程式名稱。參數不認得、缺少值或重複時丟出 CommandLineError。
CommandLineOptions parseCommandLine(std::span<const std::wstring> args);

}  // namespace tmw::core
