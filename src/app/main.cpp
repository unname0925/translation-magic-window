#include <windows.h>

#include <shellapi.h>
#include <winrt/base.h>

#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "app/app_controller.h"
#include "app/app_identity.h"
#include "core/command_line.h"
#include "platform/app_paths.h"
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

        // 螢幕擷取（WinRT）和 PNG（WIC）都需要 COM。UI 執行緒使用單一執行緒 apartment。
        winrt::init_apartment(winrt::apartment_type::single_threaded);

        tmw::app::AppController controller(instance, dataDirectory);
        return controller.run();
    } catch (const winrt::hresult_error& error) {
        showFatalError(std::wstring(error.message()));
    } catch (const std::exception& error) {
        showFatalError(tmw::platform::utf8ToWide(error.what()));
    } catch (...) {
        showFatalError(L"發生未知的錯誤。");
    }
    return 1;
}
