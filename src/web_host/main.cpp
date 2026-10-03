// tmw_web_host.exe：Chrome／Edge 的 Native Messaging 主機（網頁漫畫整頁翻譯）。
//
// 瀏覽器在擴充功能呼叫 chrome.runtime.connectNative 時啟動它，用標準輸入／輸出交換封包
// （4 位元組長度＋JSON，core/web_protocol.h）。它只負責轉送：連上主程式的具名管道
// （platform/web_pipe.h），兩個方向原封不動搬過去，不解析內容。
//
// - 主程式沒在跑時把它叫起來（同一個資料夾的 TranslationMagicWindow.exe），等它開好管道。
// - 任何一邊關閉就結束：瀏覽器關掉分頁或擴充功能、主程式結束。
// - 標準輸出只能有封包：其他的輸出會讓瀏覽器斷線，所以這裡不印任何東西。
#include <windows.h>

#include <fcntl.h>
#include <io.h>

#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>

#include "core/web_protocol.h"
#include "platform/app_paths.h"
#include "platform/web_pipe.h"

namespace {

using namespace tmw;

constexpr DWORD kFirstTryMs = 500;
// 主程式剛啟動時要先載入 OCR 模型才會開管道
constexpr DWORD kStartupWaitMs = 30000;

bool readExactly(std::FILE* in, char* data, std::size_t size) {
    return std::fread(data, 1, size, in) == size;
}

// 標準輸入的一則封包。瀏覽器關閉時回傳 nullopt
std::optional<std::string> readFromBrowser() {
    std::array<std::uint8_t, 4> header{};
    if (!readExactly(stdin, reinterpret_cast<char*>(header.data()), header.size())) {
        return std::nullopt;
    }
    const std::uint32_t length = core::frameLength(header);
    if (length > core::kWebMaxRequestBytes) {
        return std::nullopt;
    }
    std::string message(length, '\0');
    if (length > 0 && !readExactly(stdin, message.data(), length)) {
        return std::nullopt;
    }
    return message;
}

bool writeToBrowser(const std::string& message) {
    const std::string framed = core::frameMessage(message);
    return std::fwrite(framed.data(), 1, framed.size(), stdout) == framed.size() &&
           std::fflush(stdout) == 0;
}

bool launchApp() {
    const std::wstring app =
        (platform::executableDirectory() / L"TranslationMagicWindow.exe").wstring();
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    std::wstring commandLine = L"\"" + app + L"\"";
    // 不繼承標準輸入輸出：主程式不能碰瀏覽器的管道
    if (!CreateProcessW(app.c_str(), commandLine.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS,
                        nullptr, nullptr, &startup, &process)) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

HANDLE connectToApp() {
    const std::wstring name = platform::webPipeName();
    HANDLE pipe = platform::connectWebPipe(name, kFirstTryMs);
    if (pipe != INVALID_HANDLE_VALUE) {
        return pipe;
    }
    if (!launchApp()) {
        return INVALID_HANDLE_VALUE;
    }
    return platform::connectWebPipe(name, kStartupWaitMs);
}

}  // namespace

int main() {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    const HANDLE pipe = connectToApp();
    if (pipe == INVALID_HANDLE_VALUE) {
        // 擴充功能看得懂的錯誤，id 是空的：整條連線都不能用
        writeToBrowser(core::webErrorReply("", "app-unavailable"));
        return 1;
    }

    // 主程式 → 瀏覽器：主程式關閉時整個結束（標準輸入的讀取擋在另一個執行緒裡）
    std::thread fromApp([pipe] {
        while (true) {
            const std::optional<std::string> message =
                platform::readWebFrame(pipe, nullptr, core::kWebMaxReplyBytes + 1024);
            if (!message || !writeToBrowser(*message)) {
                break;
            }
        }
        ExitProcess(0);
    });
    fromApp.detach();

    // 瀏覽器 → 主程式
    while (true) {
        const std::optional<std::string> message = readFromBrowser();
        if (!message || !platform::writeWebFrame(pipe, *message)) {
            break;
        }
    }
    CloseHandle(pipe);
    ExitProcess(0);
}
