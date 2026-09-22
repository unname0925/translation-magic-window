#include "support/app_process.h"

#include <cwchar>
#include <iterator>
#include <stdexcept>

#include "app/app_identity.h"
#include "platform/text_encoding.h"
#include "support/test_window.h"

namespace tmw::test {
namespace {

using namespace std::chrono_literals;

// 依照 CommandLineToArgvW 的規則加上引號：引號前的反斜線要加倍，引號本身要跳脫
std::wstring quoteArgument(const std::wstring& arg) {
    std::wstring quoted = L"\"";
    int backslashes = 0;
    for (const wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            quoted.append(static_cast<size_t>(backslashes) * 2 + 1, L'\\');
        } else {
            quoted.append(static_cast<size_t>(backslashes), L'\\');
        }
        backslashes = 0;
        quoted.push_back(ch);
    }
    quoted.append(static_cast<size_t>(backslashes) * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

struct FindWindowQuery {
    DWORD processId = 0;
    const wchar_t* className = nullptr;
    HWND result = nullptr;
};

BOOL CALLBACK findWindowCallback(HWND hwnd, LPARAM lParam) {
    auto* query = reinterpret_cast<FindWindowQuery*>(lParam);
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId != query->processId) {
        return TRUE;
    }
    wchar_t className[256]{};
    GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));
    if (wcscmp(className, query->className) != 0) {
        return TRUE;
    }
    query->result = hwnd;
    return FALSE;
}

}  // namespace

AppProcess::AppProcess(const std::vector<std::wstring>& args) {
    const std::filesystem::path exe = executablePath();
    std::wstring commandLine = quoteArgument(exe.wstring());
    for (const std::wstring& arg : args) {
        commandLine += L' ';
        commandLine += quoteArgument(arg);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        nullptr, &startup, &info_)) {
        throw std::runtime_error("cannot start " + platform::wideToUtf8(exe.wstring()) +
                                 " (Win32 error " + std::to_string(GetLastError()) + ")");
    }
    CloseHandle(info_.hThread);
    info_.hThread = nullptr;
}

AppProcess::~AppProcess() {
    if (info_.hProcess == nullptr) {
        return;
    }
    if (!hasExited() && !requestExit(10s)) {
        TerminateProcess(info_.hProcess, 1);
        WaitForSingleObject(info_.hProcess, 5000);
    }
    CloseHandle(info_.hProcess);
}

std::filesystem::path AppProcess::executablePath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    // 所有執行檔都輸出到同一個資料夾（見 CMakeLists.txt 的 CMAKE_RUNTIME_OUTPUT_DIRECTORY）
    return std::filesystem::path(path).parent_path() / L"TranslationMagicWindow.exe";
}

bool AppProcess::isAnyInstanceRunning() {
    const HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, app::kInstanceMutexName);
    if (mutex == nullptr) {
        return false;
    }
    CloseHandle(mutex);
    return true;
}

bool AppProcess::hasExited() const {
    return WaitForSingleObject(info_.hProcess, 0) == WAIT_OBJECT_0;
}

std::optional<DWORD> AppProcess::waitForExit(std::chrono::milliseconds timeout) {
    if (!waitUntil([this] { return hasExited(); }, timeout)) {
        return std::nullopt;
    }
    DWORD exitCode = 0;
    GetExitCodeProcess(info_.hProcess, &exitCode);
    return exitCode;
}

namespace {

struct FindTitleQuery {
    DWORD processId = 0;
    const wchar_t* title = nullptr;
    HWND result = nullptr;
};

BOOL CALLBACK findTitleCallback(HWND hwnd, LPARAM lParam) {
    auto* query = reinterpret_cast<FindTitleQuery*>(lParam);
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId != query->processId || IsWindowVisible(hwnd) == FALSE) {
        return TRUE;
    }
    wchar_t title[256]{};
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    if (wcscmp(title, query->title) != 0) {
        return TRUE;
    }
    query->result = hwnd;
    return FALSE;
}

}  // namespace

HWND AppProcess::findWindowByTitle(const wchar_t* title) const {
    FindTitleQuery query{info_.dwProcessId, title, nullptr};
    EnumWindows(&findTitleCallback, reinterpret_cast<LPARAM>(&query));
    return query.result;
}

HWND AppProcess::waitForWindowByTitle(const wchar_t* title,
                                      std::chrono::milliseconds timeout) const {
    HWND hwnd = nullptr;
    waitUntil(
        [&] {
            hwnd = findWindowByTitle(title);
            return hwnd != nullptr || hasExited();
        },
        timeout);
    return hwnd;
}

HWND AppProcess::findWindow(const wchar_t* className) const {
    FindWindowQuery query{info_.dwProcessId, className};
    EnumWindows(&findWindowCallback, reinterpret_cast<LPARAM>(&query));
    return query.result;
}

HWND AppProcess::waitForWindow(const wchar_t* className, std::chrono::milliseconds timeout) const {
    HWND hwnd = nullptr;
    waitUntil(
        [&] {
            hwnd = findWindow(className);
            return hwnd != nullptr || hasExited();
        },
        timeout);
    return hwnd;
}

bool AppProcess::isResponsive(std::chrono::milliseconds timeout) const {
    const HWND controller = findWindow(app::kControllerClassName);
    return controller != nullptr &&
           SendMessageTimeoutW(controller, WM_NULL, 0, 0, SMTO_NORMAL,
                               static_cast<UINT>(timeout.count()), nullptr) != 0;
}

bool AppProcess::postCommand(UINT command) const {
    const HWND controller = findWindow(app::kControllerClassName);
    return controller != nullptr && PostMessageW(controller, WM_COMMAND, command, 0) != FALSE;
}

std::optional<DWORD> AppProcess::requestExit(std::chrono::milliseconds timeout) {
    if (!hasExited() && !postCommand(app::kCommandExit)) {
        return std::nullopt;
    }
    return waitForExit(timeout);
}

}  // namespace tmw::test
