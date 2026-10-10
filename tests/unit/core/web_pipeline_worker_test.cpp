// 網頁漫畫的工作佇列：好幾頁同時翻譯、下一頁的 OCR 和這一頁的翻譯同時做、取消、結束不卡住。
#include "core/web_pipeline_worker.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/text_converter.h"
#include "core/translator_chain.h"
#include "support/fake_clock.h"

namespace tmw::core {
namespace {

using namespace std::chrono_literals;

// 每一頁讀到不同的字（依圖片寬度），翻譯快取才不會讓後面的頁直接命中
class CountingOcr final : public IOcrService {
public:
    OcrResult recognize(const ImageBgra& frame, Language, std::stop_token) override {
        ++calls;
        return {{OcrLine{RectI{20, 20, 300, 44}, "ページ" + std::to_string(frame.width), 0.9f,
                         Orientation::Horizontal}},
                Language::Japanese};
    }
    std::atomic<int> calls{0};
};

// 翻譯要等測試放行（或等一段時間），並記下最多同時有幾個請求
class SlowTranslator final : public ITranslator {
public:
    std::string id() const override { return "slow"; }
    bool supportsBatch() const override { return true; }

    std::vector<std::string> translate(std::span<const std::string> segments,
                                       const TranslateRequest&, std::stop_token cancel) override {
        const int now = ++inside;
        int seen = most.load();
        while (now > seen && !most.compare_exchange_weak(seen, now)) {
        }
        {
            std::unique_lock lock(mutex);
            opened.wait_for(lock, cancel, hold, [this] { return open; });
        }
        --inside;
        if (cancel.stop_requested()) {
            throw TranslatorError(TranslateError::Cancelled, "cancelled");
        }
        return std::vector<std::string>(segments.begin(), segments.end());
    }

    void release() {
        {
            const std::lock_guard lock(mutex);
            open = true;
        }
        opened.notify_all();
    }

    std::chrono::milliseconds hold = 5s;
    std::atomic<int> inside{0};
    std::atomic<int> most{0};
    std::mutex mutex;
    std::condition_variable_any opened;
    bool open = false;
};

class WebPipelineWorkerTest : public ::testing::Test {
protected:
    test::FakeClock clock_;
    std::shared_ptr<SlowTranslator> engine_ = std::make_shared<SlowTranslator>();
    std::shared_ptr<TranslatorChain> chain_ = std::make_shared<TranslatorChain>(
        std::vector<std::shared_ptr<ITranslator>>{engine_}, clock_, ChainOptions{});
    TranslationService translation_{chain_, std::make_shared<NullTextConverter>()};
    CountingOcr ocr_;
    Pipeline pipeline_{ocr_, translation_};

    std::mutex mutex_;
    std::condition_variable arrived_;
    std::vector<PipelineResult> results_;

    WebPipelineWorker::OnResult collector() {
        return [this](PipelineResult result) {
            {
                const std::lock_guard lock(mutex_);
                results_.push_back(std::move(result));
            }
            arrived_.notify_all();
        };
    }

    static PipelineJob page(std::uint64_t generation) {
        PipelineJob job;
        job.generation = generation;
        job.lens = 1000;
        job.frame = ImageBgra(400 + static_cast<int>(generation), 300);
        return job;
    }

    bool waitFor(std::size_t count) {
        std::unique_lock lock(mutex_);
        return arrived_.wait_for(lock, 10s, [&] { return results_.size() >= count; });
    }

    template <typename Predicate>
    static bool eventually(Predicate predicate) {
        for (int i = 0; i < 5000; ++i) {
            if (predicate()) {
                return true;
            }
            std::this_thread::sleep_for(1ms);
        }
        return false;
    }
};

TEST_F(WebPipelineWorkerTest, SeveralPagesAreTranslatedAtOnce) {
    WebPipelineWorker worker(pipeline_, 3, collector());
    for (std::uint64_t i = 1; i <= 5; ++i) {
        worker.submit(page(i));
    }
    ASSERT_TRUE(eventually([&] { return engine_->inside.load() == 3; }))
        << "三個翻譯執行緒同時在等翻譯";
    engine_->release();
    ASSERT_TRUE(waitFor(5));
    EXPECT_EQ(engine_->most.load(), 3) << "不超過翻譯執行緒的數量";
    EXPECT_EQ(ocr_.calls.load(), 5);
}

TEST_F(WebPipelineWorkerTest, TheNextPageIsRecognizedWhileOneIsTranslating) {
    WebPipelineWorker worker(pipeline_, 1, collector());
    worker.submit(page(1));
    worker.submit(page(2));
    ASSERT_TRUE(eventually([&] { return engine_->inside.load() == 1; }));
    EXPECT_TRUE(eventually([&] { return ocr_.calls.load() == 2; }))
        << "第一頁還在翻譯，第二頁的 OCR 已經做好了";
    engine_->release();
    ASSERT_TRUE(waitFor(2));
}

TEST_F(WebPipelineWorkerTest, ACancelledPageReportsNothing) {
    WebPipelineWorker worker(pipeline_, 1, collector());
    worker.submit(page(1));
    worker.submit(page(2));
    worker.submit(page(3));
    ASSERT_TRUE(eventually([&] { return engine_->inside.load() == 1; }));
    worker.cancel(1);  // 翻譯中
    worker.cancel(3);  // 還在排隊（或 OCR 做好在等翻譯）
    engine_->release();
    ASSERT_TRUE(waitFor(1));
    ASSERT_TRUE(eventually([&] { return !worker.busy(); }));
    const std::lock_guard lock(mutex_);
    ASSERT_EQ(results_.size(), 1u);
    EXPECT_EQ(results_[0].generation, 2u);
}

TEST_F(WebPipelineWorkerTest, ResultsCarryTheTranslation) {
    engine_->release();
    WebPipelineWorker worker(pipeline_, 2, collector());
    worker.submit(page(7));
    ASSERT_TRUE(waitFor(1));
    const std::lock_guard lock(mutex_);
    ASSERT_EQ(results_[0].groups.size(), 1u);
    EXPECT_EQ(results_[0].generation, 7u);
    EXPECT_EQ(results_[0].groups[0].block.text, "ページ407");
}

TEST_F(WebPipelineWorkerTest, APageOnScreenGoesBeforePagesNotStarted) {
    WebPipelineWorker worker(pipeline_, 1, collector());
    worker.submit(page(1));
    ASSERT_TRUE(eventually([&] { return engine_->inside.load() == 1; }));
    worker.submit(page(2));
    worker.submit(page(3));
    PipelineJob urgent = page(4);
    urgent.urgent = true;  // 使用者翻到了這一頁
    worker.submit(std::move(urgent));
    engine_->release();
    ASSERT_TRUE(waitFor(4));
    const std::lock_guard lock(mutex_);
    // 1 在翻譯、2 的 OCR 已經做好在等（最多等 translators 頁），3 還沒開始
    EXPECT_EQ(results_[0].generation, 1u) << "已經在翻的照樣先完成";
    EXPECT_EQ(results_[2].generation, 4u) << "畫面上的插到還沒開始的前面";
    EXPECT_EQ(results_[3].generation, 3u);
}

TEST_F(WebPipelineWorkerTest, StoppingWhileBusyDoesNotHang) {
    WebPipelineWorker worker(pipeline_, 2, collector());
    for (std::uint64_t i = 1; i <= 6; ++i) {
        worker.submit(page(i));
    }
    ASSERT_TRUE(eventually([&] { return engine_->inside.load() == 2; }));
    const auto start = std::chrono::steady_clock::now();
    worker.stop();  // 翻譯中的會被取消
    EXPECT_LT(std::chrono::steady_clock::now() - start, 3s);
}

}  // namespace
}  // namespace tmw::core
