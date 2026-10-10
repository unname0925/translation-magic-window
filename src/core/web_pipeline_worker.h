// 網頁漫畫的工作佇列：一個辨識執行緒＋好幾個翻譯執行緒（docs/proposal-speed-and-web-manga.md）。
//
// 透鏡的 PipelineWorker 一次處理一件、從 OCR 做到翻譯完。網頁一次有整章幾十頁，那樣做的話
// 顯示卡在等翻譯、翻譯也一次只送一頁。量測（本機 Ollama hy-mt2、RTX 4070）：
// - 一頁的時間 OCR 0.2～0.5 秒、翻譯 0.7～2.1 秒，翻譯占七八成
// - Ollama 開平行處理（OLLAMA_NUM_PARALLEL=4）時，4 頁同時送 2.4 秒、依序送 5.6 秒
// 所以拆成兩段（Pipeline::recognize／finish）：
// - 辨識執行緒依序做 OCR（用顯示卡，和透鏡共用一把鎖）
// - 做好的頁交給翻譯執行緒，最多 translators 頁同時在翻譯；下一頁的 OCR 同時進行
// 交進來的順序就是處理的順序（擴充功能已經依「離畫面多近」排好）；只有 job.urgent 的
// （送來時就在畫面上）插到還沒開始的頁前面，後送來的先做：使用者翻到哪一頁就在等哪一頁。
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

#include "core/pipeline.h"

namespace tmw::core {

class WebPipelineWorker {
public:
    // 在工作執行緒上呼叫（辨識或翻譯執行緒）。被取消的工作不會呼叫
    using OnResult = std::function<void(PipelineResult)>;

    WebPipelineWorker(Pipeline& pipeline, int translators, OnResult onResult);
    ~WebPipelineWorker();

    WebPipelineWorker(const WebPipelineWorker&) = delete;
    WebPipelineWorker& operator=(const WebPipelineWorker&) = delete;

    // job.generation 用來取消和對回結果，要每件不同
    void submit(PipelineJob job);
    // 取消一件（排隊中的丟掉、進行中的請它停下來）
    void cancel(std::uint64_t generation);
    void stop();

    // 測試用：還有工作在排隊或進行中
    bool busy() const;

private:
    struct Item {
        PipelineJob job;
        std::stop_source cancel;
        Pipeline::RecognizedPage page;
    };

    void recognizeLoop();
    void translateLoop();
    void deliver(const std::shared_ptr<Item>& item, PipelineResult result);

    Pipeline& pipeline_;
    OnResult onResult_;
    const std::size_t translators_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::shared_ptr<Item>> waiting_;              // 等著做 OCR
    std::deque<std::shared_ptr<Item>> recognized_;           // OCR 做好、等著翻譯
    std::map<std::uint64_t, std::shared_ptr<Item>> active_;  // 所有還沒結束的（取消用）
    std::size_t translating_ = 0;
    bool stopping_ = false;
    std::vector<std::thread> threads_;
};

}  // namespace tmw::core
