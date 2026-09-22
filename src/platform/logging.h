// 記錄（見 docs/design.md 4.12）。
//
// - 記錄檔在 <資料夾>\logs\translation-magic-window.log，輪替保留最近幾個檔案。
// - 每筆記錄都可以帶透鏡編號和流水號，方便追蹤同一次處理的完整過程。
// - **隱私**：擷取到的文字、譯文和金鑰預設不寫入。要寫這類內容時一律用 sensitive()，
//   只有使用者打開「詳細診斷」後才會真的寫出去（design.md 4.12）。
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace tmw::platform {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

struct LogOptions {
    std::filesystem::path directory;  // <資料夾>\logs
    LogLevel level = LogLevel::Info;
    bool verboseDiagnostics = false;      // 設定檔的同名欄位
    bool alsoToStderr = false;            // 開發時方便看
    std::size_t maxFileBytes = 5u << 20;  // 每個記錄檔 5 MB
    std::size_t maxFiles = 3;
};

// 建立（或重設）全域的記錄器。重複呼叫會換成新的設定，測試因此可以指向暫存資料夾。
void initializeLogging(const LogOptions& options);

// 關掉記錄器並把緩衝寫出去。程式結束前呼叫。
void shutdownLogging();

// 使用者有沒有打開詳細診斷（沒有初始化時是 false）
bool verboseDiagnostics();

// 會被記錄的一段內容。敏感內容（原文、譯文、金鑰）一律包成 sensitive(...)：
// 詳細診斷關閉時寫出「（略）」，打開時才寫出真正的內容。
std::string sensitive(std::string_view text);

// 一次處理的識別：透鏡編號和流水號（design.md 4.12）
struct LogContext {
    int lens = 0;
    std::uint64_t sequence = 0;
};

// 記錄檔一律是 UTF-8。path.string() 會用系統字碼頁轉換，中文路徑會變成問號，所以路徑一律用這個。
std::string pathToUtf8(const std::filesystem::path& path);

void log(LogLevel level, std::string_view message);
void log(LogLevel level, const LogContext& context, std::string_view message);

inline void logInfo(std::string_view message) {
    log(LogLevel::Info, message);
}
inline void logWarn(std::string_view message) {
    log(LogLevel::Warn, message);
}
inline void logError(std::string_view message) {
    log(LogLevel::Error, message);
}

}  // namespace tmw::platform
