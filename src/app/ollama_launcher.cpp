#include "app/ollama_launcher.h"

#include <windows.h>

#include <shlobj.h>

#include <filesystem>

#include "app/translation_setup.h"
#include "net/cpr_http_client.h"
#include "platform/logging.h"

namespace tmw::app {
namespace {

constexpr auto kMinimumInterval = std::chrono::minutes(1);

// Ollama 的安裝程式預設裝在 %LOCALAPPDATA%\Programs\Ollama
std::filesystem::path ollamaApp() {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder))) {
        CoTaskMemFree(folder);
        return {};
    }
    std::filesystem::path path =
        std::filesystem::path(folder) / L"Programs" / L"Ollama" / L"ollama app.exe";
    CoTaskMemFree(folder);
    return path;
}

bool answers() {
    net::CprHttpClient http;
    net::HttpRequest request;
    request.url = "http://127.0.0.1:11434/api/version";
    request.timeout = std::chrono::milliseconds(1500);
    return http.send(request, {}).status == 200;
}

void launch() {
    const std::filesystem::path app = ollamaApp();
    if (app.empty() || !std::filesystem::exists(app)) {
        platform::logWarn(
            "本機的 Ollama 沒有回應，也找不到 "
            "Ollama（%LOCALAPPDATA%\\Programs\\Ollama）：請手動開啟");
        return;
    }
    std::wstring commandLine = L"\"" + app.wstring() + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(app.c_str(), commandLine.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS,
                        nullptr, nullptr, &startup, &process)) {
        platform::logWarn("本機的 Ollama 沒有回應，啟動 Ollama 失敗");
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    platform::logInfo("本機的 Ollama 沒有回應：已經啟動 Ollama");
}

}  // namespace

OllamaLauncher::~OllamaLauncher() = default;  // jthread 解構時等檢查做完（最多約 1.5 秒）

void OllamaLauncher::ensureRunning(const std::string& endpoint) {
    if (!isLocalOllama(endpoint)) {
        return;
    }
    const std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (checked_ && now - lastCheck_ < kMinimumInterval) {
        return;
    }
    checked_ = true;
    lastCheck_ = now;
    if (check_.joinable()) {
        check_.join();  // 上一次的檢查早就結束了（間隔至少一分鐘）
    }
    check_ = std::jthread([] {
        if (!answers()) {
            launch();
        }
    });
}

}  // namespace tmw::app
