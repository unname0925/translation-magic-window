// 網頁漫畫整頁翻譯：接收瀏覽器擴充功能送來的圖片，排進處理管線，把覆蓋資料送回去
// （docs/proposal-speed-and-web-manga.md 第二部分）。
//
// - 管道的執行緒只解析訊息（base64 解碼這種重活不佔 UI 執行緒），其餘狀態都在 UI 執行緒上。
// - 交給網頁自己的工作佇列（core/web_pipeline_worker.h）：OCR 一頁一頁做、翻譯好幾頁同時送。
//   同時在處理的最多 kInFlight 頁，其餘在這裡排隊（擴充功能已經依離畫面多近排好順序）。
// - 一律當漫畫處理（找對話框、直排對白用 manga-ocr），不管透鏡的漫畫模式設定。
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "core/pipeline.h"
#include "core/web_protocol.h"
#include "platform/web_pipe.h"

namespace tmw::app {

class WebService {
public:
    // 網頁的工作用的透鏡編號（真的透鏡從 1 開始，最多 4 個）
    static constexpr int kLensId = 1000;
    // 同時交給工作佇列幾頁：翻譯中的（工作佇列的翻譯執行緒數）加上一兩頁先做 OCR
    static constexpr std::size_t kInFlight = 6;

    struct Callbacks {
        // 在 UI 執行緒上執行（管道的執行緒用它把事情交回 UI 執行緒）
        std::function<void(std::function<void()>)> post;
        // 填好語言、專有名詞表、背景修補等設定（UI 執行緒）
        std::function<void(core::PipelineJob&)> configure;
        std::function<void(core::PipelineJob)> submit;
        std::function<void(std::uint64_t generation)> cancel;  // 取消進行中的一頁
        std::function<void(const std::string&)> log;
    };

    WebService(std::wstring pipeName, Callbacks callbacks);
    ~WebService();

    WebService(const WebService&) = delete;
    WebService& operator=(const WebService&) = delete;

    // 開始接受擴充功能的連線。管道開不起來（另一個主程式已經在跑）時回傳 false
    bool start();
    // start 失敗的原因（Windows 的錯誤碼）
    unsigned long startError() const { return server_.lastError(); }

    // UI 執行緒：處理管線送回的結果
    void onResult(const core::PipelineResult& result);

    // UI 執行緒：工作佇列要重建了（改設定），進行中的頁不會有結果：回報錯誤讓擴充功能重試
    void abandonRunning(const std::string& reason);

    // 測試用：還有幾張在排隊（不含進行中的）
    std::size_t queued() const { return queue_.size(); }

private:
    struct Pending {
        int connection = 0;
        std::string id;
        std::shared_ptr<core::WebRequest> request;
    };
    struct Running {
        int connection = 0;
        std::string id;
    };

    void onMessage(int connection, std::string message);
    void onDisconnect(int connection);
    // 以下都在 UI 執行緒
    void enqueue(int connection, std::shared_ptr<core::WebRequest> request);
    void cancel(int connection, const std::string& id);
    void dropConnection(int connection);
    void pump();
    void submitNext();

    Callbacks callbacks_;
    platform::WebPipeServer server_;
    std::deque<Pending> queue_;
    std::map<std::uint64_t, Running> running_;  // generation → 誰要的
    std::uint64_t nextGeneration_ = 0;
    // 交回 UI 執行緒的工作拿弱參照：這個物件不在了就不執行
    std::shared_ptr<bool> alive_;
};

}  // namespace tmw::app
