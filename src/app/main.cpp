#include <windows.h>

#include <shellapi.h>
#include <winrt/base.h>

#include <QApplication>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "app/app_controller.h"
#include "app/app_identity.h"
#include "core/command_line.h"
#include "platform/app_paths.h"
#include "platform/logging.h"
#include "platform/settings_file.h"
#include "platform/single_instance.h"
#include "platform/text_encoding.h"

namespace {

constexpr wchar_t kAppTitle[] = L"Translation Magic Window";

void showFatalError(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), kAppTitle, MB_OK | MB_ICONERROR);
}

// 命令列參數，不含程式名稱
std::vector<std::wstring> commandLineArguments() {
    int count = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (argv == nullptr) {
        return {};
    }
    std::vector<std::wstring> args(argv + (count > 0 ? 1 : 0), argv + count);
    LocalFree(argv);
    return args;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        // 參數有錯時先報錯，即使已經有執行個體在跑
        const tmw::core::CommandLineOptions options =
            tmw::core::parseCommandLine(commandLineArguments());

        const tmw::platform::SingleInstance singleInstance(tmw::app::kInstanceMutexName);
        if (!singleInstance.isFirst()) {
            // 已經在執行：請它把透鏡顯示出來，自己直接結束
            tmw::app::AppController::notifyRunningInstance();
            return 0;
        }

        const std::filesystem::path dataDirectory =
            options.dataDirectory ? std::filesystem::absolute(*options.dataDirectory)
                                  : tmw::platform::defaultDataDirectory();

        // 設定要先讀，記錄的「詳細診斷」由設定決定（design.md 4.11、4.12）
        const std::filesystem::path settingsPath =
            options.dataDirectory ? tmw::platform::settingsPathIn(dataDirectory)
                                  : tmw::platform::defaultSettingsPath();
        const tmw::platform::SettingsFileLoad settings = tmw::platform::loadSettings(settingsPath);
        tmw::platform::initializeLogging(
            {.directory = dataDirectory / L"logs",
             .verboseDiagnostics = settings.settings.verboseDiagnostics});
        tmw::platform::logInfo("Translation Magic Window 啟動，資料夾：" +
                               tmw::platform::pathToUtf8(dataDirectory));
        if (!settings.problem.empty()) {
            tmw::platform::logWarn("設定檔改用了預設值：" + settings.problem);
        }
        if (settings.missing) {
            // 第一次啟動就寫出預設設定檔，使用者才知道有哪些選項可以改
            tmw::platform::saveSettings(settingsPath, settings.settings);
            tmw::platform::logInfo("建立了預設的設定檔：" +
                                   tmw::platform::pathToUtf8(settingsPath));
        }

        // 螢幕擷取（WinRT）和 PNG（WIC）都需要 COM。UI 執行緒使用單一執行緒 apartment。
        winrt::init_apartment(winrt::apartment_type::single_threaded);

        // Qt 的事件迴圈同時處理 Win32 的訊息，所以 QApplication 要先建立起來
        int argc = 0;
        const QApplication application(argc, nullptr);

        tmw::app::AppController controller(instance, dataDirectory, settingsPath,
                                           settings.settings);
        const int code = controller.run();
        tmw::platform::logInfo("結束");
        tmw::platform::shutdownLogging();
        return code;
    } catch (const winrt::hresult_error& error) {
        showFatalError(std::wstring(error.message()));
    } catch (const std::exception& error) {
        showFatalError(tmw::platform::utf8ToWide(error.what()));
    } catch (...) {
        showFatalError(L"發生未知的錯誤。");
    }
    return 1;
}
