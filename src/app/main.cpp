#include <windows.h>

#include <winrt/base.h>

#include <exception>
#include <string>

#include "app/app_controller.h"
#include "platform/single_instance.h"
#include "platform/text_encoding.h"

namespace {

constexpr wchar_t kAppTitle[] = L"Translation Magic Window";

void showFatalError(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), kAppTitle, MB_OK | MB_ICONERROR);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        const tmw::platform::SingleInstance singleInstance(
            L"Local\\TranslationMagicWindow.Instance");
        if (!singleInstance.isFirst()) {
            // 已經在執行：請它把透鏡顯示出來，自己直接結束
            tmw::app::AppController::notifyRunningInstance();
            return 0;
        }

        // 螢幕擷取（WinRT）和 PNG（WIC）都需要 COM。UI 執行緒使用單一執行緒 apartment。
        winrt::init_apartment(winrt::apartment_type::single_threaded);

        tmw::app::AppController controller(instance);
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
