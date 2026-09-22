// 工作執行緒：排隊、取代過時的工作、取消進行中的工作、結束時不卡住。
#include "core/pipeline_worker.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/translator_chain.h"
#include "support/fake_clock.h"

namespace tmw::core {
namespace {

using namespace std::chrono_literals;

// 測試用的閂：讓測試精準控制 OCR 什麼時候完成。
// 用 condition_variable_any 的 stop_token 版本，取消時會馬上醒來
// （真正的 OCR 是自己輪詢 token，這裡只是要讓測試不必等逾時）。
class Latch {
public:
    // ignoreCancel：連取消也不放行。用在「要確保這件工作一直卡著」的測試，
    // 否則取消會馬上把它叫醒，後面排隊的工作就跑掉了。
    void wait(std::stop_token cancel, bool ignoreCancel) {
        std::unique_lock lock(mutex_);
        if (ignoreCancel) {
            opened_.wait_for(lock, 5s, [this] { return open_; });
        } else {
            opened_.wait(lock, std::move(cancel), [this] { return open_; });
        }
    }

    void open() {
        {
            const std::lock_guard lock(mutex_);
            open_ = true;
        }
        opened_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable_any opened_;
    bool open_ = false;
};

class BlockingOcr final : public IOcrService {
public:
    std::vector<OcrLine> recognize(const ImageBgra&, std::stop_token cancel) override {
        // 先取好閂再宣告「開始了」：測試看到 started 之後才會把 latch 換掉，
        // 反過來寫的話，Release 版有機會在這中間溜過去，這件工作就不會被擋住
        Latch* waiting = latch.load();
        ++started;
        if (waiting != nullptr) {
            waiting->wait(cancel, latchIgnoresCancel.load());
        }
        if (cancel.stop_requested()) {
            ++cancelled;
            return {};
        }
        return {OcrLine{RectI{20, 20, 300, 44}, "こんにちは", 0.9f, Orientation::Horizontal}};
    }

    std::atomic<int> started{0};
    std::atomic<int> cancelled{0};
    std::atomic<Latch*> latch{nullptr};
    std::atomic<bool> latchIgnoresCancel{false};
};

class PassThroughTranslator final : public ITranslator {
public:
    std::string id() const override { return "fake"; }
    bool supportsBatch() const override { return true; }
    std::vector<std::string> translate(std::span<const std::string> segments,
                                       const TranslateRequest&, std::stop_token) override {
        return std::vector<std::string>(segments.begin(), segments.end());
    }
};

class PipelineWorkerTest : public ::testing::Test {
protected:
    test::FakeClock clock_;
    std::shared_ptr<TranslatorChain> chain_ = std::make_shared<TranslatorChain>(
        std::vector<std::shared_ptr<ITranslator>>{std::make_shared<PassThroughTranslator>()},
        clock_, ChainOptions{});
    TranslationService translation_{chain_, std::make_shared<NullTextConverter>()};
    BlockingOcr ocr_;
    Pipeline pipeline_{ocr_, translation_};

    std::mutex mutex_;
    std::condition_variable arrived_;
    std::vector<PipelineResult> results_;

    PipelineWorker::OnResult collector() {
        return [this](PipelineResult result) {
            {
                const std::lock_guard lock(mutex_);
                results_.push_back(std::move(result));
            }
            arrived_.notify_all();
        };
    }

    static PipelineJob job(std::uint64_t generation, int lens = 1) {
        PipelineJob out;
        out.generation = generation;
        out.lens = lens;
        out.frame = ImageBgra(320, 240);
        return out;
    }

    void waitUntilRunning(int count = 1) {
        for (int i = 0; i < 5000 && ocr_.started < count; ++i) {
            std::this_thread::sleep_for(1ms);
        }
        ASSERT_GE(ocr_.started.load(), count);
    }

    bool waitForResults(std::size_t count) {
        std::unique_lock lock(mutex_);
        return arrived_.wait_for(lock, 5s, [this, count] { return results_.size() >= count; });
    }

    std::vector<PipelineResult> results() {
        const std::lock_guard lock(mutex_);
        return results_;
    }
};

TEST_F(PipelineWorkerTest, RunsAJobAndReportsTheResult) {
    PipelineWorker worker(pipeline_, collector());
    worker.submit(job(1));
    ASSERT_TRUE(waitForResults(1));
    const std::vector<PipelineResult> out = results();
    EXPECT_EQ(out[0].generation, 1u);
    ASSERT_EQ(out[0].groups.size(), 1u);
    EXPECT_EQ(out[0].groups[0].block.text, "こんにちは");
}

TEST_F(PipelineWorkerTest, ANewJobCancelsTheOneInFlight) {
    Latch latch;
    ocr_.latch = &latch;
    PipelineWorker worker(pipeline_, collector());
    worker.submit(job(1));
    waitUntilRunning();

    ocr_.latch = nullptr;  // 第二件不用等
    worker.submit(job(2));
    ASSERT_TRUE(waitForResults(1));
    latch.open();

    const std::vector<PipelineResult> out = results();
    ASSERT_EQ(out.size(), 1u) << "被取消的那件不會送回結果";
    EXPECT_EQ(out[0].generation, 2u);
    EXPECT_EQ(ocr_.cancelled, 1);
}

TEST_F(PipelineWorkerTest, OnlyTheNewestQueuedJobPerLensSurvives) {
    Latch latch;
    // 第一件要一直卡著。它被取消時如果馬上結束，第 2 件就會在第 3 件送出之前跑完，
    // 測到的就不是「排隊中只留最新的一件」了。
    ocr_.latchIgnoresCancel = true;
    ocr_.latch = &latch;
    PipelineWorker worker(pipeline_, collector());
    worker.submit(job(1));  // 開始跑，卡在閂上
    waitUntilRunning();
    ocr_.latch = nullptr;
    worker.submit(job(2));  // 排隊
    worker.submit(job(3));  // 取代排隊中的第 2 件
    latch.open();

    ASSERT_TRUE(waitForResults(1));
    std::this_thread::sleep_for(50ms);  // 給可能多出來的結果一點時間
    const std::vector<PipelineResult> out = results();
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].generation, 3u) << "中間那件一定過時了，不必做";
    EXPECT_EQ(ocr_.started, 2) << "第 2 件從頭到尾沒有被做過";
}

TEST_F(PipelineWorkerTest, JobsForDifferentLensesBothRun) {
    PipelineWorker worker(pipeline_, collector());
    worker.submit(job(1, 1));
    worker.submit(job(1, 2));
    ASSERT_TRUE(waitForResults(2));
    const std::vector<PipelineResult> out = results();
    EXPECT_NE(out[0].lens, out[1].lens) << "一個透鏡的新工作不該蓋掉另一個透鏡的";
}

TEST_F(PipelineWorkerTest, CancelDropsTheQueuedJob) {
    Latch latch;
    ocr_.latch = &latch;
    PipelineWorker worker(pipeline_, collector());
    worker.submit(job(1, 1));
    waitUntilRunning();
    worker.submit(job(1, 2));
    worker.cancel(2);
    ocr_.latch = nullptr;
    latch.open();

    ASSERT_TRUE(waitForResults(1));
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(results().size(), 1u);
}

TEST_F(PipelineWorkerTest, CancelStopsTheJobInFlight) {
    Latch latch;
    ocr_.latch = &latch;
    PipelineWorker worker(pipeline_, collector());
    worker.submit(job(1));
    waitUntilRunning();
    worker.cancel(1);

    // 取消會叫醒卡住的 OCR，不必等閂打開
    for (int i = 0; i < 5000 && ocr_.cancelled == 0; ++i) {
        std::this_thread::sleep_for(1ms);
    }
    EXPECT_EQ(ocr_.cancelled, 1);
    std::this_thread::sleep_for(50ms);
    EXPECT_TRUE(results().empty());
    latch.open();
}

TEST_F(PipelineWorkerTest, StoppingWhileBusyDoesNotHang) {
    Latch latch;
    ocr_.latch = &latch;
    PipelineWorker worker(pipeline_, collector());
    worker.submit(job(1));
    waitUntilRunning();
    worker.stop();  // 解構也會做，這裡先做一次確認不會卡住
    EXPECT_FALSE(worker.busy());
    latch.open();
}

TEST_F(PipelineWorkerTest, SubmittingAfterStopDoesNothing) {
    // 關閉程式時透鏡可能還在送工作。執行緒已經結束了，這些工作不該堆在佇列裡。
    PipelineWorker worker(pipeline_, collector());
    worker.stop();
    worker.submit(job(1));
    worker.submit(job(2));
    std::this_thread::sleep_for(50ms);
    EXPECT_TRUE(results().empty());
    EXPECT_EQ(ocr_.started, 0);
    EXPECT_FALSE(worker.busy()) << "停掉之後不該再收工作";
}

}  // namespace
}  // namespace tmw::core
