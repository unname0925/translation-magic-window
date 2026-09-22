// IT-08：端到端。測試視窗顯示已知的英文句子 → 擷取 → 真的 OCR → 假的翻譯引擎，
// 檢查處理管線交出來的「原文／譯文成組」和預期一致。
#include "core/pipeline.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include "core/pipeline_worker.h"
#include "core/translator_chain.h"
#include "ocr/ocr_service.h"
#include "platform/screen_capture.h"
#include "support/test_window.h"

namespace tmw {
namespace {

using namespace std::chrono_literals;
using test::TestWindow;

constexpr auto kTimeout = 2000ms;

core::RectI primaryMonitorRect() {
    return core::RectI::fromXYWH(0, 0, GetSystemMetrics(SM_CXSCREEN),
                                 GetSystemMetrics(SM_CYSCREEN));
}

std::filesystem::path modelsDirectory() {
    // 測試執行檔在 build/<preset>/bin/<config>/，模型在倉庫根目錄的 models/
    std::filesystem::path here = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::exists(here / "models" / "PP-OCRv6_medium_det")) {
            return here / "models";
        }
        if (!here.has_parent_path() || here.parent_path() == here) {
            break;
        }
        here = here.parent_path();
    }
    return {};
}

// 假引擎：在原文前面加上標記，這樣就看得出哪一段對到哪一段
class MarkingTranslator final : public core::ITranslator {
public:
    std::string id() const override { return "fake"; }
    bool supportsBatch() const override { return true; }

    std::vector<std::string> translate(std::span<const std::string> segments,
                                       const core::TranslateRequest& request,
                                       std::stop_token) override {
        language = request.srcLang;
        std::vector<std::string> out;
        out.reserve(segments.size());
        for (const std::string& segment : segments) {
            out.push_back("[譯]" + segment);
        }
        return out;
    }

    std::string language;
};

class SystemClock final : public core::IClock {
public:
    core::TimePoint now() const override { return std::chrono::steady_clock::now(); }
};

class PipelineIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        models_ = modelsDirectory();
        if (models_.empty()) {
            GTEST_SKIP() << "找不到 models/，請先執行 tools/fetch_models";
        }
    }

    const core::RectI monitor_ = primaryMonitorRect();
    // 避開螢幕中央（透鏡預設出現的位置）
    const core::RectI target_ =
        core::RectI::fromXYWH(monitor_.left + 100, monitor_.top + 100, 520, 200);
    std::filesystem::path models_;
};

TEST_F(PipelineIntegrationTest, ReadsAndTranslatesTextOnScreen) {
    TestWindow::Options options;
    options.mode = TestWindow::Mode::Text;
    options.lines = {L"The quick brown fox", L"jumps over the lazy dog"};
    const TestWindow window(target_, options);
    test::waitForComposition();

    platform::ScreenCapture capture;
    const auto frame = capture.readRegion(target_, kTimeout);
    ASSERT_TRUE(frame.has_value());

    ocr::OcrService ocr(models_, ocr::TextLanguage::JapaneseOrEnglish, ocr::Device::Auto);
    auto engine = std::make_shared<MarkingTranslator>();
    const SystemClock clock;
    auto chain = std::make_shared<core::TranslatorChain>(
        std::vector<std::shared_ptr<core::ITranslator>>{engine}, clock, core::ChainOptions{});
    core::TranslationService translation(chain, std::make_shared<core::NullTextConverter>());
    core::Pipeline pipeline(ocr, translation);

    core::PipelineJob job;
    job.generation = 42;
    job.lens = 1;
    job.region = target_;
    job.frame = *frame;

    const core::PipelineResult result = pipeline.run(job, std::stop_token{});

    EXPECT_EQ(result.generation, 42u);
    EXPECT_EQ(result.language, core::Language::English);
    EXPECT_EQ(engine->language, "en");
    EXPECT_TRUE(result.error.empty());
    ASSERT_FALSE(result.groups.empty());

    // 兩行靠得很近，應該被合併成同一段
    std::string everything;
    for (const core::TranslatedBlock& group : result.groups) {
        everything += group.block.text;
        everything += ' ';
        EXPECT_EQ(group.translation, "[譯]" + group.block.text) << "譯文要對到正確的原文";
        EXPECT_FALSE(group.block.rect.empty());
    }
    EXPECT_NE(everything.find("quick brown fox"), std::string::npos) << "實際讀到：" << everything;
    EXPECT_NE(everything.find("lazy dog"), std::string::npos) << "實際讀到：" << everything;
}

TEST_F(PipelineIntegrationTest, TheWorkerDeliversTheResultOfTheNewestJob) {
    TestWindow::Options options;
    options.mode = TestWindow::Mode::Text;
    options.lines = {L"Press E to open"};
    const TestWindow window(target_, options);
    test::waitForComposition();

    platform::ScreenCapture capture;
    const auto frame = capture.readRegion(target_, kTimeout);
    ASSERT_TRUE(frame.has_value());

    ocr::OcrService ocr(models_, ocr::TextLanguage::JapaneseOrEnglish, ocr::Device::Auto);
    auto engine = std::make_shared<MarkingTranslator>();
    const SystemClock clock;
    auto chain = std::make_shared<core::TranslatorChain>(
        std::vector<std::shared_ptr<core::ITranslator>>{engine}, clock, core::ChainOptions{});
    core::TranslationService translation(chain, std::make_shared<core::NullTextConverter>());
    core::Pipeline pipeline(ocr, translation);

    std::vector<core::PipelineResult> results;
    std::mutex mutex;
    {
        core::PipelineWorker worker(pipeline, [&](core::PipelineResult result) {
            const std::lock_guard lock(mutex);
            results.push_back(std::move(result));
        });
        for (std::uint64_t generation = 1; generation <= 3; ++generation) {
            core::PipelineJob job;
            job.generation = generation;
            job.lens = 1;
            job.region = target_;
            job.frame = *frame;
            worker.submit(std::move(job));
        }
        ASSERT_TRUE(test::waitUntil(
            [&] {
                const std::lock_guard lock(mutex);
                return !results.empty();
            },
            30s));
    }

    const std::lock_guard lock(mutex);
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results.back().generation, 3u) << "最後交出來的一定是最新那件";
    EXPECT_FALSE(results.back().groups.empty());
}

}  // namespace
}  // namespace tmw
