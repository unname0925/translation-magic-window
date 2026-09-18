#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

namespace tmw::core {

// 命令列參數（見 docs/design.md 4.10）。
struct CommandLineOptions {
    // --data-dir <資料夾>：程式自己寫出的檔案（擷取的 PNG，之後還有設定和記錄檔）
    // 改放到這個資料夾。主要給自動化測試用，讓測試不會動到使用者的資料。
    std::optional<std::filesystem::path> dataDirectory;
};

class CommandLineError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

// args 不含程式名稱。參數不認得、缺少值或重複時丟出 CommandLineError。
CommandLineOptions parseCommandLine(std::span<const std::wstring> args);

}  // namespace tmw::core
