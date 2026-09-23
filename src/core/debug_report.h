// 除錯傾印的內容（見 docs/design.md 4.12）。
//
// 使用者按下快捷鍵時，程式把「當下發生了什麼」存成一個資料夾交給我們看：
// 擷取到的畫面、OCR 讀到的每一行、合併後的段落、譯文、各步驟的耗時，以及設定。
//
// 這個檔案只負責把這些東西整理成 JSON，不碰檔案系統，所以可以單獨測試。
// **金鑰一定會被拿掉**：傾印是要寄給別人看的，設定裡的 encryptedApiKey 換成「（已移除）」。
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/pipeline.h"
#include "core/settings.h"
#include "core/text_layout.h"

namespace tmw::core {

// 設定的副本，金鑰換成固定的字串。傾印和記錄都用它。
Settings withoutSecrets(Settings settings);

struct DebugReportInput {
    std::string appVersion;
    std::string time;       // 當地時間，例如 "2026-09-22 13:45:01"
    std::string ocrDevice;  // "DirectML"、"CPU"…
    // 引擎鏈目前的狀況（例如「google：被限流或額度用完，約 4 分鐘後再試」）
    std::string engineStatus;
    Settings settings;
    // 最後一次處理的結果。還沒翻譯過任何東西時是空的。
    std::optional<PipelineResult> lastResult;
    // 合併成段落之前，OCR 讀到的每一行。查「為什麼這句被切開」時最有用。
    std::vector<OcrLine> lastLines;
    // 各步驟耗時的統計（core/perf_stats.h 的報告）。空的就不寫進去。
    std::string perfReport;
};

// 整理成 JSON（縮排過，人看得懂）
std::string buildDebugReport(const DebugReportInput& input);

}  // namespace tmw::core
