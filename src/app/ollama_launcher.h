// 設定的翻譯引擎是本機的 Ollama、但它沒在跑時把它叫起來。
//
// 2026-10-04：電腦重開機後 Ollama 沒有自動啟動（啟動資料夾裡沒有它的捷徑），
// 網頁漫畫的每一頁都翻譯失敗。
// 不去改使用者的開機設定，而是主程式需要它的時候（啟動、改設定、翻譯因為連不上而失敗）檢查一下：
// 在背景執行緒連 /api/version，連不上就啟動 Ollama 自己的 ollama app.exe（它會照使用者的環境變數，
// 例如 OLLAMA_NUM_PARALLEL，開 ollama serve）。一分鐘最多試一次。
//
// 2026-10-12：Ollama 只在載入模型的那一刻決定放多少到顯示卡（net/ollama_status.h），晚載入的話
// 常常只放得下一部分、翻譯慢十倍。所以 Ollama 一回應就先把設定的模型載入（主程式剛啟動、
// 顯示記憶體還空），之後定期看一下，沒放滿就趁沒在翻譯時卸載再載入。
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace tmw::app {

class OllamaLauncher {
public:
    OllamaLauncher() = default;
    ~OllamaLauncher();

    OllamaLauncher(const OllamaLauncher&) = delete;
    OllamaLauncher& operator=(const OllamaLauncher&) = delete;

    // endpoint 是本機 Ollama 的網址時才做事：沒在跑就啟動，然後載入 model。不會卡住呼叫的執行緒
    void ensureRunning(const std::string& endpoint, const std::string& model);

    // 定期呼叫（例如每兩分鐘）：模型沒整個放在顯示卡上、而且 idle（沒在翻譯）時卸載再載入，
    // 十分鐘最多重新載入一次。不會卡住呼叫的執行緒；上一次還沒做完就略過
    void checkPlacement(const std::string& endpoint, const std::string& model, bool idle);

private:
    // 在背景做 work；上一個還在做就略過，回傳 false
    bool startWork(std::function<void(std::stop_token)> work);

    std::mutex mutex_;
    std::chrono::steady_clock::time_point lastCheck_{};
    bool checked_ = false;
    std::chrono::steady_clock::time_point lastReload_{};
    bool reloaded_ = false;
    bool warnedPartial_ = false;
    std::atomic<bool> working_{false};
    std::jthread check_;
};

}  // namespace tmw::app
