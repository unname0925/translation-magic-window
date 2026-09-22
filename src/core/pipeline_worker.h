// 處理管線的佇列和工作執行緒（見 docs/design.md 3.3）。
//
// 推論依序執行，避免多個透鏡同時搶 GPU。排隊中的工作每個透鏡只留最新的一件：
// 舊的那件一定已經過時了（畫面或透鏡位置已經變了），做完也會被丟掉。
// 同一個透鏡送來新工作時，進行中的那件會被取消。
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

#include "core/pipeline.h"

namespace tmw::core {

class PipelineWorker {
public:
    // 在工作執行緒上呼叫。UI 要自己排隊回到 UI 執行緒（Qt 的 queued signal）。
    // 被取消的工作不會呼叫這個回呼。
    using OnResult = std::function<void(PipelineResult)>;

    PipelineWorker(Pipeline& pipeline, OnResult onResult);
    ~PipelineWorker();

    PipelineWorker(const PipelineWorker&) = delete;
    PipelineWorker& operator=(const PipelineWorker&) = delete;

    // 取代同一個透鏡還在排隊的工作，並取消它進行中的工作
    void submit(PipelineJob job);

    // 取消某個透鏡的工作（排隊中的丟掉，進行中的請它停下來）
    void cancel(int lens);

    // 停掉執行緒。解構時會自動呼叫。
    void stop();

    // 測試用：還有沒有工作在排隊或進行中
    bool busy() const;

private:
    void loop();

    Pipeline& pipeline_;
    OnResult onResult_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<PipelineJob> queue_;  // 每個透鏡最多一件
    std::optional<int> runningLens_;
    std::stop_source runningCancel_;
    bool stopping_ = false;
    std::thread thread_;
};

}  // namespace tmw::core
