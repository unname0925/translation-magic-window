#include "app/ollama_launcher.h"

#include <windows.h>

#include <shlobj.h>

#include <filesystem>

#include "app/translation_setup.h"
#include "net/cpr_http_client.h"
#include "net/ollama_status.h"
#include "platform/logging.h"

namespace tmw::app {
namespace {

constexpr auto kMinimumInterval = std::chrono::minutes(1);
// 重新載入要十幾秒、那段時間不能翻譯：放不滿（被別的程式占著）時不要一直重試
constexpr auto kMinimumReloadInterval = std::chrono::minutes(10);

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

std::string megabytes(std::uint64_t bytes) {
    return std::to_string(bytes / (1024 * 1024)) + " MB";
}

std::string describe(const net::OllamaPlacement& placement) {
    return megabytes(placement.onGpu) + "／" + megabytes(placement.size);
}

}  // namespace

// jthread 解構時請停止並等它做完（載入模型的請求會被取消）
OllamaLauncher::~OllamaLauncher() = default;

bool OllamaLauncher::startWork(std::function<void(std::stop_token)> work) {
    if (working_.exchange(true)) {
        return false;
    }
    if (check_.joinable()) {
        check_.join();  // 上一個已經做完（working_ 是 false），只是收回執行緒
    }
    check_ = std::jthread([this, work = std::move(work)](std::stop_token stop) {
        work(stop);
        working_ = false;
    });
    return true;
}

void OllamaLauncher::ensureRunning(const std::string& endpoint, const std::string& model) {
    if (!isLocalOllama(endpoint)) {
        return;
    }
    const std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (checked_ && now - lastCheck_ < kMinimumInterval) {
        return;
    }
    if (!startWork([model](std::stop_token stop) {
            if (!answers()) {
                launch();
                // 等它起來（最多 30 秒）再載入模型
                for (int i = 0; i < 30 && !stop.stop_requested() && !answers(); ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
            if (model.empty() || stop.stop_requested()) {
                return;
            }
            // 趁顯示記憶體還空的時候先載入（主程式之後長大不會把它擠出去）
            net::CprHttpClient http;
            if (!net::setOllamaKeepAlive(http, model, kOllamaKeepAlive, stop)) {
                return;
            }
            if (const auto placement = net::ollamaPlacement(http, model, stop)) {
                platform::logInfo(
                    "本機的 Ollama 先載入了 " + model + "：顯示卡上 " + describe(*placement) +
                    (placement->fullyOnGpu() ? "" : "（沒放滿，其餘用 CPU 算，會比較慢）"));
            }
        })) {
        return;  // 上一次的檢查還在做
    }
    checked_ = true;
    lastCheck_ = now;
}

void OllamaLauncher::checkPlacement(const std::string& endpoint, const std::string& model,
                                    bool idle) {
    if (!isLocalOllama(endpoint) || model.empty()) {
        return;
    }
    const std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    const bool mayReload = idle && (!reloaded_ || now - lastReload_ >= kMinimumReloadInterval);
    startWork([this, model, mayReload, now](std::stop_token stop) {
        net::CprHttpClient http;
        const net::OllamaPlacementCheck check =
            net::checkOllamaPlacement(http, model, kOllamaKeepAlive, mayReload, stop);
        if (check.action == net::OllamaPlacementAction::NotLoaded ||
            check.action == net::OllamaPlacementAction::Full) {
            return;
        }
        const std::lock_guard relock(mutex_);
        if (check.after.has_value()) {
            reloaded_ = true;
            lastReload_ = now;
        }
        if (check.action == net::OllamaPlacementAction::Reloaded) {
            platform::logInfo("本機的 Ollama：" + model + " 只有 " + describe(*check.before) +
                              " 在顯示卡上（其餘用 CPU 算，翻譯慢很多），重新載入後整個放進去了（" +
                              describe(*check.after) + "）");
            warnedPartial_ = false;
        } else if (check.after.has_value() && !warnedPartial_) {
            platform::logWarn(
                "本機的 Ollama：" + model + " 只有 " + describe(*check.after) +
                " 放得進顯示卡，重新載入也一樣：顯示記憶體被其他程式占著，翻譯會比較慢");
            warnedPartial_ = true;
        }
    });
}

}  // namespace tmw::app
