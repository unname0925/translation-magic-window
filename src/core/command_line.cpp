#include "core/command_line.h"

namespace tmw::core {
namespace {

// 錯誤訊息用 UTF-8；core 不依賴 Win32，所以借用 std::filesystem 做 UTF-16 → UTF-8 的轉換
std::string toUtf8(const std::wstring& text) {
    const std::u8string utf8 = std::filesystem::path(text).u8string();
    return std::string(utf8.begin(), utf8.end());
}

}  // namespace

CommandLineOptions parseCommandLine(std::span<const std::wstring> args) {
    CommandLineOptions options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::wstring& arg = args[i];
        if (arg == L"--data-dir") {
            if (options.dataDirectory) {
                throw CommandLineError("--data-dir is specified more than once");
            }
            if (i + 1 >= args.size() || args[i + 1].empty()) {
                throw CommandLineError("--data-dir requires a folder path");
            }
            options.dataDirectory = std::filesystem::path(args[++i]);
        } else if (arg == L"--ocr-device") {
            if (options.ocrDevice) {
                throw CommandLineError("--ocr-device is specified more than once");
            }
            if (i + 1 >= args.size()) {
                throw CommandLineError("--ocr-device requires cpu, dml or auto");
            }
            const std::string value = toUtf8(args[++i]);
            if (value != "cpu" && value != "dml" && value != "auto") {
                throw CommandLineError("--ocr-device must be cpu, dml or auto, not " + value);
            }
            options.ocrDevice = value;
        } else {
            throw CommandLineError("unknown command-line option: " + toUtf8(arg));
        }
    }
    return options;
}

}  // namespace tmw::core
