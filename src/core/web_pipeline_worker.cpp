#include "core/web_pipeline_worker.h"

#include <algorithm>
#include <utility>

namespace tmw::core {

WebPipelineWorker::WebPipelineWorker(Pipeline& pipeline, int translators, OnResult onResult)
    : pipeline_(pipeline),
      onResult_(std::move(onResult)),
      translators_(static_cast<std::size_t>(std::max(1, translators))) {
    threads_.emplace_back([this] { recognizeLoop(); });
    for (std::size_t i = 0; i < translators_; ++i) {
        threads_.emplace_back([this] { translateLoop(); });
    }
}

WebPipelineWorker::~WebPipelineWorker() {
    stop();
}

void WebPipelineWorker::submit(PipelineJob job) {
    auto item = std::make_shared<Item>();
    item->job = std::move(job);
    {
        const std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        active_[item->job.generation] = item;
        if (item->job.urgent) {
            waiting_.push_front(std::move(item));
        } else {
            waiting_.push_back(std::move(item));
        }
    }
    wake_.notify_all();
}

void WebPipelineWorker::cancel(std::uint64_t generation) {
    {
        const std::lock_guard lock(mutex_);
        const auto found = active_.find(generation);
        if (found == active_.end()) {
            return;
        }
        found->second->cancel.request_stop();
        const auto same = [generation](const std::shared_ptr<Item>& item) {
            return item->job.generation == generation;
        };
        std::erase_if(waiting_, same);
        std::erase_if(recognized_, same);
        active_.erase(found);
    }
    wake_.notify_all();
}

void WebPipelineWorker::stop() {
    {
        const std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
        for (auto& [generation, item] : active_) {
            item->cancel.request_stop();
        }
        waiting_.clear();
        recognized_.clear();
        active_.clear();
    }
    wake_.notify_all();
    for (std::thread& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

bool WebPipelineWorker::busy() const {
    const std::lock_guard lock(mutex_);
    return !active_.empty();
}

void WebPipelineWorker::deliver(const std::shared_ptr<Item>& item, PipelineResult result) {
    {
        const std::lock_guard lock(mutex_);
        const auto found = active_.find(item->job.generation);
        if (found == active_.end() || found->second != item) {
            return;  // 已經取消了
        }
        active_.erase(found);
    }
    if (!item->cancel.get_token().stop_requested() && onResult_) {
        onResult_(std::move(result));
    }
}

void WebPipelineWorker::recognizeLoop() {
    while (true) {
        std::shared_ptr<Item> item;
        {
            std::unique_lock lock(mutex_);
            // 翻譯跟不上時先不要往前做太多 OCR：做好的頁最多等 translators_ 頁
            wake_.wait(lock, [this] {
                return stopping_ || (!waiting_.empty() && recognized_.size() < translators_);
            });
            if (stopping_) {
                return;
            }
            item = std::move(waiting_.front());
            waiting_.pop_front();
        }
        Pipeline::RecognizedPage page = pipeline_.recognize(item->job, item->cancel.get_token());
        if (page.done) {
            deliver(item, std::move(page.result));  // 沒有文字、或被取消了
            continue;
        }
        {
            const std::lock_guard lock(mutex_);
            if (stopping_) {
                return;
            }
            if (!active_.contains(item->job.generation)) {
                continue;  // 做 OCR 的時候被取消了
            }
            item->page = std::move(page);
            if (item->job.urgent) {
                recognized_.push_front(item);
            } else {
                recognized_.push_back(item);
            }
        }
        wake_.notify_all();
    }
}

void WebPipelineWorker::translateLoop() {
    while (true) {
        std::shared_ptr<Item> item;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !recognized_.empty(); });
            if (stopping_) {
                return;
            }
            item = std::move(recognized_.front());
            recognized_.pop_front();
            ++translating_;
        }
        wake_.notify_all();  // 有空位了：辨識執行緒可以做下一頁
        PipelineResult result =
            pipeline_.finish(std::move(item->page), item->job, item->cancel.get_token());
        {
            const std::lock_guard lock(mutex_);
            --translating_;
        }
        deliver(item, std::move(result));
    }
}

}  // namespace tmw::core
