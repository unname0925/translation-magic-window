#include "core/pipeline_worker.h"

#include <algorithm>
#include <utility>

namespace tmw::core {

PipelineWorker::PipelineWorker(Pipeline& pipeline, OnResult onResult)
    : pipeline_(pipeline), onResult_(std::move(onResult)) {
    thread_ = std::thread([this] { loop(); });
}

PipelineWorker::~PipelineWorker() {
    stop();
}

void PipelineWorker::submit(PipelineJob job) {
    {
        const std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        // 同一個透鏡進行中的工作已經過時了。例外：進行中的是預先做的 OCR、送來的是正式處理——
        // 那是同一個畫面（畫面變了的話送來的會是新的預先工作），讓它做完，正式處理排在後面直接沿用
        const bool preparingForThis = runningPrepare_ && job.prepareTicket == 0;
        if (runningLens_ == job.lens && !preparingForThis) {
            runningCancel_.request_stop();
        }
        const auto same = std::find_if(queue_.begin(), queue_.end(),
                                       [&job](const PipelineJob& q) { return q.lens == job.lens; });
        if (same != queue_.end()) {
            *same = std::move(job);
        } else {
            queue_.push_back(std::move(job));
        }
    }
    wake_.notify_one();
}

void PipelineWorker::cancel(int lens) {
    const std::lock_guard lock(mutex_);
    std::erase_if(queue_, [lens](const PipelineJob& job) { return job.lens == lens; });
    if (runningLens_ == lens) {
        runningCancel_.request_stop();
    }
}

void PipelineWorker::stop() {
    {
        const std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
        queue_.clear();
        runningCancel_.request_stop();
    }
    wake_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool PipelineWorker::busy() const {
    const std::lock_guard lock(mutex_);
    return !queue_.empty() || runningLens_.has_value();
}

void PipelineWorker::loop() {
    while (true) {
        PipelineJob job;
        std::stop_token cancel;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) {
                return;
            }
            job = std::move(queue_.front());
            queue_.erase(queue_.begin());
            runningLens_ = job.lens;
            runningPrepare_ = job.prepareTicket != 0;
            runningCancel_ = std::stop_source();
            cancel = runningCancel_.get_token();
        }

        PipelineResult result = pipeline_.run(job, cancel);

        {
            const std::lock_guard lock(mutex_);
            runningLens_.reset();
            runningPrepare_ = false;
            if (stopping_) {
                return;
            }
        }
        // 被取消的結果一定過時了（畫面或透鏡位置已經變了），不要送回去。
        // 預先做的 OCR 記在 Pipeline 裡，沒有結果要送
        if (!cancel.stop_requested() && job.prepareTicket == 0 && onResult_) {
            onResult_(std::move(result));
        }
    }
}

}  // namespace tmw::core
