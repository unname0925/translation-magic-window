// 除錯傾印的資料夾（見 docs/design.md 4.12）。
//
// 使用者按下 Ctrl+Alt+Shift+D 時，把「現在發生了什麼」存成一個資料夾，直接壓縮寄出來就能查：
//
//   dumps\debug-20260922-134501\
//     capture.png   透鏡底下當時的畫面
//     report.json   OCR 讀到的每一行、合併後的段落、譯文、耗時、設定（金鑰已移除）
//     logs\         記錄檔（報告只有最後一次處理，記錄檔看得到之前發生過什麼）
//
// 內容怎麼整理在 core/debug_report.h，這裡只負責建資料夾和寫檔。
#pragma once

#include <chrono>
#include <filesystem>

#include "core/debug_report.h"
#include "core/image.h"

namespace tmw::platform {

// 資料夾的名字，例如 debug-20260922-134501（本地時間）
std::wstring debugDumpFolderName(std::chrono::system_clock::time_point time);

struct DebugDumpContents {
    core::DebugReportInput report;
    // 透鏡底下的畫面。擷取不到時留 nullptr，報告照樣寫。
    const core::ImageBgra* capture = nullptr;
    // 記錄檔的資料夾。空的或不存在時就不附記錄檔。
    std::filesystem::path logsDirectory;
};

// 在 dumpsDirectory 底下建一個以時間命名的資料夾並寫進內容。
// 回傳資料夾的位置；連資料夾或報告都寫不出來時回傳空路徑。
std::filesystem::path writeDebugDump(const std::filesystem::path& dumpsDirectory,
                                     const DebugDumpContents& contents,
                                     std::chrono::system_clock::time_point time);

}  // namespace tmw::platform
