#include "platform/crash_dump.h"

#include <windows.h>

#include <dbghelp.h>

#include <atomic>
#include <cstddef>
#include <ctime>
#include <cwchar>

namespace tmw::platform {
namespace {

// 檔名的格式只寫一次，處理常式和 dumpFileName 共用，兩邊不會走樣
constexpr const wchar_t* kFileNameFormat = L"%s-%04d%02d%02d-%02d%02d%02d.dmp";
// 「<資料夾>\<前綴>-20260922-134501.dmp」放得下
constexpr std::size_t kPathCapacity = 32768;
constexpr std::size_t kNameCapacity = 64;

// 安裝時就決定好的傾印資料夾。處理常式只會讀它，不配置記憶體。
wchar_t g_directory[kPathCapacity] = {};
std::size_t g_directoryLength = 0;
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;
std::atomic<bool> g_installed{false};
// 同時當掉的兩個執行緒只寫一份，檔案不會互相蓋掉
std::atomic_flag g_writing = ATOMIC_FLAG_INIT;

int formatFileName(wchar_t (&buffer)[kNameCapacity], const wchar_t* prefix,
                   const SYSTEMTIME& time) {
    return std::swprintf(buffer, kNameCapacity, kFileNameFormat, prefix, time.wYear, time.wMonth,
                         time.wDay, time.wHour, time.wMinute, time.wSecond);
}

// 寫出傾印檔。不配置記憶體：路徑組在呼叫端給的緩衝區裡。
bool writeTo(const wchar_t* path, _EXCEPTION_POINTERS* exception) {
    const HANDLE file =
        CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    MINIDUMP_EXCEPTION_INFORMATION information{};
    information.ThreadId = GetCurrentThreadId();
    information.ExceptionPointers = exception;
    information.ClientPointers = FALSE;

    // 夠查出「在哪裡、為什麼」又不會太大：執行緒資訊、堆疊指到的記憶體、卸載過的模組。
    // 不含完整的記憶體內容，傾印檔通常只有幾 MB，也不會把使用者的文字整份帶走。
    const MINIDUMP_TYPE type =
        static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory |
                                   MiniDumpWithUnloadedModules);
    const BOOL ok =
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                          exception == nullptr ? nullptr : &information, nullptr, nullptr);
    CloseHandle(file);
    if (ok == FALSE) {
        DeleteFileW(path);  // 半個檔案只會誤導人
        return false;
    }
    return true;
}

// 把 <資料夾>\<檔名> 組進 buffer。放不下時回傳 false。
bool joinPath(wchar_t (&buffer)[kPathCapacity], const wchar_t* directory,
              std::size_t directoryLength, const wchar_t* name, std::size_t nameLength) {
    if (directoryLength + 1 + nameLength + 1 > kPathCapacity) {
        return false;
    }
    std::wmemcpy(buffer, directory, directoryLength);
    buffer[directoryLength] = L'\\';
    std::wmemcpy(buffer + directoryLength + 1, name, nameLength);
    buffer[directoryLength + 1 + nameLength] = L'\0';
    return true;
}

LONG WINAPI handleUnhandledException(_EXCEPTION_POINTERS* exception) {
    if (!g_writing.test_and_set()) {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t name[kNameCapacity] = {};
        const int nameLength = formatFileName(name, L"crash", now);
        wchar_t path[kPathCapacity] = {};
        if (nameLength > 0 && joinPath(path, g_directory, g_directoryLength, name,
                                       static_cast<std::size_t>(nameLength))) {
            writeTo(path, exception);
        }
    }
    // 交還給系統：Windows 的錯誤回報照常出現，使用者看得到程式當掉了
    return g_previousFilter == nullptr ? EXCEPTION_CONTINUE_SEARCH : g_previousFilter(exception);
}

}  // namespace

std::filesystem::path dumpsDirectory(const std::filesystem::path& dataDirectory) {
    return dataDirectory / L"dumps";
}

std::wstring dumpFileName(std::wstring_view prefix, int year, int month, int day, int hour,
                          int minute, int second) {
    const std::wstring prefixText(prefix);
    SYSTEMTIME time{};
    time.wYear = static_cast<WORD>(year);
    time.wMonth = static_cast<WORD>(month);
    time.wDay = static_cast<WORD>(day);
    time.wHour = static_cast<WORD>(hour);
    time.wMinute = static_cast<WORD>(minute);
    time.wSecond = static_cast<WORD>(second);
    wchar_t buffer[kNameCapacity] = {};
    const int length = formatFileName(buffer, prefixText.c_str(), time);
    return length > 0 ? std::wstring(buffer, static_cast<std::size_t>(length)) : std::wstring();
}

std::wstring dumpFileName(std::wstring_view prefix, std::chrono::system_clock::time_point time) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(time);
    std::tm local{};
    if (localtime_s(&local, &seconds) != 0) {
        return std::wstring();
    }
    return dumpFileName(prefix, local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                        local.tm_hour, local.tm_min, local.tm_sec);
}

std::optional<std::filesystem::path> writeCrashDump(_EXCEPTION_POINTERS* exception,
                                                    const std::filesystem::path& directory) {
    std::error_code ignored;
    std::filesystem::create_directories(directory, ignored);
    const std::filesystem::path path =
        directory / dumpFileName(L"crash", std::chrono::system_clock::now());
    if (!writeTo(path.c_str(), exception)) {
        return std::nullopt;
    }
    return path;
}

bool installCrashHandler(const std::filesystem::path& directory) {
    std::error_code ignored;
    std::filesystem::create_directories(directory, ignored);

    const std::wstring text = directory.wstring();
    // 留給「\」、檔名和結尾的 0
    if (text.size() + 1 + kNameCapacity >= kPathCapacity) {
        return false;
    }
    std::wmemcpy(g_directory, text.c_str(), text.size() + 1);
    g_directoryLength = text.size();

    if (!g_installed.exchange(true)) {
        g_previousFilter = SetUnhandledExceptionFilter(handleUnhandledException);
    }
    return true;
}

void removeCrashHandler() {
    if (g_installed.exchange(false)) {
        SetUnhandledExceptionFilter(g_previousFilter);
        g_previousFilter = nullptr;
    }
    g_writing.clear();
}

}  // namespace tmw::platform
