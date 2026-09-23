#include "platform/debug_dump.h"

#include <cstdio>
#include <ctime>
#include <cwchar>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>

#include "platform/logging.h"
#include "platform/png_file.h"
#include "platform/text_encoding.h"

namespace tmw::platform {

std::wstring debugDumpFolderName(std::chrono::system_clock::time_point time) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(time);
    std::tm local{};
    if (localtime_s(&local, &seconds) != 0) {
        return L"debug";
    }
    wchar_t name[64] = {};
    std::swprintf(name, std::size(name), L"debug-%04d%02d%02d-%02d%02d%02d", local.tm_year + 1900,
                  local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    return name;
}

std::filesystem::path writeDebugDump(const std::filesystem::path& dumpsDirectory,
                                     const DebugDumpContents& contents,
                                     std::chrono::system_clock::time_point time) {
    std::filesystem::path folder = dumpsDirectory / debugDumpFolderName(time);
    std::error_code failed;
    // 同一秒按兩次也要各自留下來，不然第二次會蓋掉第一次
    for (int attempt = 2; std::filesystem::exists(folder) && attempt < 100; ++attempt) {
        folder = dumpsDirectory / (debugDumpFolderName(time) + L"-" + std::to_wstring(attempt));
    }
    std::filesystem::create_directories(folder, failed);
    if (failed) {
        logWarn("建不出除錯傾印的資料夾：" + failed.message());
        return {};
    }

    // 報告是這個資料夾的重點，寫不出來就算失敗
    {
        std::ofstream report(folder / L"report.json", std::ios::binary);
        report << core::buildDebugReport(contents.report);
        if (!report) {
            logWarn("除錯傾印寫不出 report.json");
            return {};
        }
    }

    // 畫面和記錄檔是加分的：少了它們報告還是有用，所以失敗只記錄下來
    if (contents.capture != nullptr && !contents.capture->empty()) {
        try {
            savePng(*contents.capture, folder / L"capture.png");
        } catch (const std::exception& error) {
            logWarn(std::string("除錯傾印存不了畫面：") + error.what());
        }
    }
    if (!contents.logsDirectory.empty() && std::filesystem::is_directory(contents.logsDirectory)) {
        std::filesystem::copy(contents.logsDirectory, folder / L"logs",
                              std::filesystem::copy_options::overwrite_existing, failed);
        if (failed) {
            logWarn("除錯傾印複製不了記錄檔：" + failed.message());
        }
    }

    logInfo("除錯傾印：" + pathToUtf8(folder));
    return folder;
}

}  // namespace tmw::platform
