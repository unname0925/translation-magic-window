// 當機傾印（見 docs/design.md 4.12）。
//
// 程式發生未處理的例外時，把當下的狀態寫成 minidump 放進 <資料夾>\dumps，
// 之後用 Visual Studio 或 WinDbg 打開就看得到當時的呼叫堆疊。
//
// 處理常式在程式已經壞掉的狀態下執行，所以它不配置記憶體、不寫記錄檔（spdlog 有鎖，
// 當機的執行緒可能正握著它），資料夾也在安裝時就先建好。寫完之後交還給系統，
// Windows 的錯誤回報照常出現，使用者不會覺得程式無聲無息地消失。
#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

struct _EXCEPTION_POINTERS;

namespace tmw::platform {

// 傾印檔放在 <資料夾>\dumps
std::filesystem::path dumpsDirectory(const std::filesystem::path& dataDirectory);

// 傾印檔的檔名，例如 dumpFileName(L"crash", 2026, 9, 22, 13, 45, 1) → crash-20260922-134501.dmp
std::wstring dumpFileName(std::wstring_view prefix, int year, int month, int day, int hour,
                          int minute, int second);

// 用本地時間取檔名
std::wstring dumpFileName(std::wstring_view prefix, std::chrono::system_clock::time_point time);

// 把當下的狀態寫成 minidump，回傳寫出的檔案；失敗時回傳 nullopt。
// exception 可以是 nullptr（不是從例外處理常式呼叫時），這時傾印的是呼叫端自己的狀態。
std::optional<std::filesystem::path> writeCrashDump(_EXCEPTION_POINTERS* exception,
                                                    const std::filesystem::path& directory);

// 安裝未處理例外的處理常式，傾印檔寫到 directory（資料夾會先建好）。
// 路徑太長放不進固定緩衝區時回傳 false，程式照常執行、只是沒有傾印。
bool installCrashHandler(const std::filesystem::path& directory);

// 換回安裝前的處理常式。測試用；正式程式裝上去就不拿掉。
void removeCrashHandler();

}  // namespace tmw::platform
