// 網頁漫畫整頁翻譯：接收瀏覽器擴充功能送來的圖片，排進處理管線，把覆蓋資料送回去
// （docs/proposal-speed-and-web-manga.md 第二部分）。
//
// - 管道的執行緒只解析訊息（base64 解碼這種重活不佔 UI 執行緒），其餘狀態都在 UI 執行緒上。
// - 一次只送一張圖進共用的工作佇列，用專屬的透鏡編號（kLensId）。
//   同一章的頁面依序處理、共用翻譯的上下文；透鏡的工作最多只要等一張網頁的圖。
// - 一律當漫畫處理（找對話框、直排對白用 manga-ocr），不管透鏡的漫畫模式設定。
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
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

    struct Callbacks {
        // 在 UI 執行緒上執行（管道的執行緒用它把事情交回 UI 執行緒）
        std::function<void(std::function<void()>)> post;
        // 填好語言、專有名詞表、背景修補等設定（UI 執行緒）
        std::function<void(core::PipelineJob&)> configure;
        std::function<void(core::PipelineJob)> submit;
        std::function<void()> cancel;  // 取消進行中的網頁工作
        std::function<void(const std::string&)> log;
    };

    WebService(std::wstring pipeName, Callbacks callbacks);
    ~WebService();

    WebService(const WebService&) = delete;
    WebService& operator=(const WebService&) = delete;

    // 開始接受擴充功能的連線。管道開不起來（另一個主程式已經在跑）時回傳 false
    bool start();

    // UI 執行緒：處理管線送回 kLensId 的結果
    void onResult(const core::PipelineResult& result);

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
        std::uint64_t generation = 0;
    };

    void onMessage(int connection, std::string message);
    void onDisconnect(int connection);
    // 以下都在 UI 執行緒
    void enqueue(int connection, std::shared_ptr<core::WebRequest> request);
    void cancel(int connection, const std::string& id);
    void dropConnection(int connection);
    void pump();

    Callbacks callbacks_;
    platform::WebPipeServer server_;
    std::deque<Pending> queue_;
    std::optional<Running> running_;
    std::uint64_t nextGeneration_ = 0;
    // 交回 UI 執行緒的工作拿弱參照：這個物件不在了就不執行
    std::shared_ptr<bool> alive_;
};

}  // namespace tmw::app
