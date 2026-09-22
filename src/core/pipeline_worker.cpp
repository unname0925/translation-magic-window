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
        // 同一個透鏡進行中的工作已經過時了
        if (runningLens_ == job.lens) {
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
            runningCancel_ = std::stop_source();
            cancel = runningCancel_.get_token();
        }

        PipelineResult result = pipeline_.run(job, cancel);

        {
            const std::lock_guard lock(mutex_);
            runningLens_.reset();
            if (stopping_) {
                return;
            }
        }
        // 被取消的結果一定過時了（畫面或透鏡位置已經變了），不要送回去
        if (!cancel.stop_requested() && onResult_) {
            onResult_(std::move(result));
        }
    }
}

}  // namespace tmw::core
