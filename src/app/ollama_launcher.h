// 設定的翻譯引擎是本機的 Ollama、但它沒在跑時把它叫起來。
//
// 2026-10-04：電腦重開機後 Ollama
// 沒有自動啟動（啟動資料夾裡沒有它的捷徑），網頁漫畫的每一頁都翻譯失敗。
// 不去改使用者的開機設定，而是主程式需要它的時候（啟動、改設定、翻譯因為連不上而失敗）檢查一下：
// 在背景執行緒連 /api/version，連不上就啟動 Ollama 自己的 ollama app.exe（它會照使用者的環境變數，
// 例如 OLLAMA_NUM_PARALLEL，開 ollama serve）。一分鐘最多試一次。
#pragma once

#include <chrono>
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

    // endpoint 是本機 Ollama 的網址時才做事。不會卡住呼叫的執行緒
    void ensureRunning(const std::string& endpoint);

private:
    std::mutex mutex_;
    std::chrono::steady_clock::time_point lastCheck_{};
    bool checked_ = false;
    std::jthread check_;
};

}  // namespace tmw::app
