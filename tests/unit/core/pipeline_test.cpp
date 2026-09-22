// 處理管線：OCR → 合併段落 → 判斷語言 → 翻譯，以及取消和失敗的處理。
#include "core/pipeline.h"

#include <gtest/gtest.h>

#include <memory>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "core/translator_chain.h"
#include "support/fake_clock.h"

namespace tmw::core {
namespace {

using Strings = std::vector<std::string>;

OcrLine line(int left, int top, int right, int bottom, std::string text) {
    return OcrLine{RectI{left, top, right, bottom}, std::move(text), 0.9f, Orientation::Horizontal};
}

// 假的 OCR：回傳事先準備好的文字行，並記下被呼叫幾次
class FakeOcr final : public IOcrService {
public:
    std::vector<OcrLine> recognize(const ImageBgra& frame, std::stop_token cancel) override {
        ++calls;
        lastSize = SizeI{frame.width, frame.height};
        if (cancel.stop_requested()) {
            return {};
        }
        return lines;
    }

    std::vector<OcrLine> lines;
    int calls = 0;
    SizeI lastSize;
};

class FakeTranslator final : public ITranslator {
public:
    std::string id() const override { return "fake"; }
    bool supportsBatch() const override { return true; }

    std::vector<std::string> translate(std::span<const std::string> segments,
                                       const TranslateRequest& request, std::stop_token) override {
        requests.push_back(request);
        batches.emplace_back(segments.begin(), segments.end());
        if (failure) {
            throw TranslatorError(*failure, "假引擎故意失敗");
        }
        std::vector<std::string> out;
        out.reserve(segments.size());
        for (const std::string& segment : segments) {
            out.push_back("譯:" + segment);
        }
        return out;
    }

    std::vector<Strings> batches;
    std::vector<TranslateRequest> requests;
    std::optional<TranslateError> failure;
};

class PipelineTest : public ::testing::Test {
protected:
    std::shared_ptr<FakeTranslator> engine_ = std::make_shared<FakeTranslator>();
    test::FakeClock clock_;
    std::shared_ptr<TranslatorChain> chain_ = std::make_shared<TranslatorChain>(
        std::vector<std::shared_ptr<ITranslator>>{engine_}, clock_, ChainOptions{});
    TranslationService translation_{chain_, std::make_shared<NullTextConverter>()};
    FakeOcr ocr_;
    Pipeline pipeline_{ocr_, translation_};

    PipelineJob job(int width = 400, int height = 300) {
        PipelineJob out;
        out.generation = 7;
        out.lens = 1;
        out.region = RectI{100, 100, 100 + width, 100 + height};
        out.frame = ImageBgra(width, height);
        return out;
    }

    PipelineResult run(const PipelineJob& request) {
        return pipeline_.run(request, std::stop_token{});
    }
};

TEST_F(PipelineTest, TurnsLinesIntoTranslatedGroups) {
    ocr_.lines = {line(20, 20, 200, 44, "Hello there"), line(20, 48, 190, 72, "friend"),
                  line(20, 150, 200, 174, "Another block")};
    const PipelineResult result = run(job());

    ASSERT_EQ(result.groups.size(), 2u) << "相鄰的兩行是同一段，隔很遠的是另一段";
    EXPECT_EQ(result.groups[0].block.text, "Hello there friend");
    EXPECT_EQ(result.groups[0].translation, "譯:Hello there friend");
    EXPECT_EQ(result.groups[1].block.text, "Another block");
    EXPECT_EQ(result.language, Language::English);
    EXPECT_EQ(result.generation, 7u);
    EXPECT_EQ(result.lens, 1);
    EXPECT_TRUE(result.error.empty());
}

TEST_F(PipelineTest, DropsBlocksCutOffByTheLensEdge) {
    ocr_.lines = {line(20, 20, 200, 44, "完整的句子"), line(0, 150, 200, 174, "被切掉的")};
    const PipelineResult result = run(job());
    ASSERT_EQ(result.groups.size(), 1u);
    EXPECT_EQ(result.groups[0].block.text, "完整的句子");
}

TEST_F(PipelineTest, KeepsEdgeBlocksWhenAskedTo) {
    PipelineOptions options;
    options.dropEdgeBlocks = false;
    Pipeline pipeline(ocr_, translation_, options);
    ocr_.lines = {line(0, 150, 200, 174, "被切掉的")};
    const PipelineResult result = pipeline.run(job(), std::stop_token{});
    EXPECT_EQ(result.groups.size(), 1u);
}

TEST_F(PipelineTest, DetectsTheLanguageFromTheWholeFrame) {
    // 單一段落常常太短，所以整片一起判斷（M0-11）
    ocr_.lines = {line(20, 20, 60, 44, "110"), line(20, 150, 300, 174, "こんにちは")};
    const PipelineResult result = run(job());
    EXPECT_EQ(result.language, Language::Japanese);
    ASSERT_FALSE(engine_->requests.empty());
    EXPECT_EQ(engine_->requests[0].srcLang, "ja");
}

TEST_F(PipelineTest, ForcedLanguageWins) {
    ocr_.lines = {line(20, 20, 300, 44, "Hello")};
    PipelineJob request = job();
    request.language = "ko";
    const PipelineResult result = pipeline_.run(request, std::stop_token{});
    EXPECT_EQ(result.language, Language::Korean);
    EXPECT_EQ(engine_->requests[0].srcLang, "ko");
}

TEST_F(PipelineTest, MarksTheSameContentAsUnchanged) {
    // 畫面閃了一下又回到原樣：不必新增歷史卡片
    ocr_.lines = {line(20, 20, 300, 44, "こんにちは")};
    EXPECT_FALSE(run(job()).unchanged);
    EXPECT_TRUE(run(job()).unchanged);

    ocr_.lines = {line(20, 20, 300, 44, "さようなら")};
    EXPECT_FALSE(run(job()).unchanged);
}

TEST_F(PipelineTest, ForgettingALensMakesTheNextResultNew) {
    ocr_.lines = {line(20, 20, 300, 44, "こんにちは")};
    run(job());
    pipeline_.forget(1);
    EXPECT_FALSE(run(job()).unchanged) << "透鏡被拖到別的地方就不算同一份內容";
}

TEST_F(PipelineTest, DifferentLensesRememberSeparately) {
    ocr_.lines = {line(20, 20, 300, 44, "こんにちは")};
    run(job());
    PipelineJob other = job();
    other.lens = 2;
    EXPECT_FALSE(pipeline_.run(other, std::stop_token{}).unchanged);
}

TEST_F(PipelineTest, AnEmptyFrameDoesNoWork) {
    PipelineJob request = job();
    request.frame = ImageBgra{};
    const PipelineResult result = pipeline_.run(request, std::stop_token{});
    EXPECT_TRUE(result.empty());
    EXPECT_EQ(ocr_.calls, 0);
}

TEST_F(PipelineTest, NoTextMeansNoTranslation) {
    ocr_.lines = {};
    const PipelineResult result = run(job());
    EXPECT_TRUE(result.empty());
    EXPECT_TRUE(engine_->batches.empty());
}

TEST_F(PipelineTest, CancellingBeforeOcrSkipsEverything) {
    std::stop_source source;
    source.request_stop();
    ocr_.lines = {line(20, 20, 300, 44, "こんにちは")};
    const PipelineResult result = pipeline_.run(job(), source.get_token());
    EXPECT_TRUE(result.empty());
    EXPECT_EQ(ocr_.calls, 0);
    EXPECT_TRUE(engine_->batches.empty());
}

TEST_F(PipelineTest, ShowsTheSourceTextWhenTranslationFails) {
    // 翻譯失敗時原文還是要看得到，並且說明原因
    engine_->failure = TranslateError::Network;
    ocr_.lines = {line(20, 20, 300, 44, "こんにちは")};
    const PipelineResult result = run(job());
    ASSERT_EQ(result.groups.size(), 1u);
    EXPECT_EQ(result.groups[0].block.text, "こんにちは");
    EXPECT_TRUE(result.groups[0].translation.empty());
    EXPECT_FALSE(result.error.empty());
}

TEST_F(PipelineTest, CancelledTranslationIsNotAnError) {
    engine_->failure = TranslateError::Cancelled;
    ocr_.lines = {line(20, 20, 300, 44, "こんにちは")};
    const PipelineResult result = run(job());
    EXPECT_TRUE(result.empty());
    EXPECT_TRUE(result.error.empty()) << "使用者自己取消的，不是錯誤";
}

TEST_F(PipelineTest, PassesRecentGroupsAsContext) {
    ocr_.lines = {line(20, 20, 300, 44, "こんにちは")};
    run(job());
    ocr_.lines = {line(20, 20, 300, 44, "さようなら")};
    run(job());

    ASSERT_EQ(engine_->requests.size(), 2u);
    EXPECT_TRUE(engine_->requests[0].context.empty());
    ASSERT_EQ(engine_->requests[1].context.size(), 1u);
    EXPECT_EQ(engine_->requests[1].context[0].first, "こんにちは");
    EXPECT_EQ(engine_->requests[1].context[0].second, "譯:こんにちは");
}

TEST_F(PipelineTest, KeepsOnlyTheMostRecentContext) {
    PipelineOptions options;
    options.contextGroups = 2;
    Pipeline pipeline(ocr_, translation_, options);
    for (const char* text : {"一つ", "二つ", "三つ", "四つ"}) {
        ocr_.lines = {line(20, 20, 300, 44, text)};
        pipeline.run(job(), std::stop_token{});
    }
    EXPECT_EQ(engine_->requests.back().context.size(), 2u);
    EXPECT_EQ(engine_->requests.back().context.front().first, "二つ");
}

TEST_F(PipelineTest, SendsRubyMarkersToTheTranslator) {
    // ルビ 不是獨立的一段，而是標在本文上一起送出去翻（design.md 4.5）
    ocr_.lines = {OcrLine{RectI{200, 50, 240, 170}, "本気で戦う", 0.9f, Orientation::Vertical, {}},
                  OcrLine{RectI{238, 50, 256, 98}, "マジ", 0.9f, Orientation::Vertical, {}}};
    const PipelineResult result = run(job());

    ASSERT_EQ(result.groups.size(), 1u) << "ルビ 不該自己成為一段";
    ASSERT_EQ(engine_->batches.size(), 1u);
    ASSERT_EQ(engine_->batches[0].size(), 1u);
    EXPECT_NE(engine_->batches[0][0].find("{本気|マジ}"), std::string::npos)
        << "實際送出：" << engine_->batches[0][0];
    EXPECT_EQ(result.groups[0].block.text, "本気で戦う") << "原文本身不帶標記";
    ASSERT_EQ(result.groups[0].block.ruby.size(), 1u);
    EXPECT_EQ(result.groups[0].block.ruby[0].reading, "マジ");
}

TEST_F(PipelineTest, MeasuresEachStep) {
    ocr_.lines = {line(20, 20, 300, 44, "こんにちは")};
    const PipelineResult result = run(job());
    EXPECT_GE(result.timings.ocrMs, 0.0);
    EXPECT_GE(result.timings.layoutMs, 0.0);
    EXPECT_GE(result.timings.translationMs, 0.0);
    EXPECT_GE(result.timings.totalMs(), result.timings.ocrMs);
}

TEST_F(PipelineTest, PassesTheFrameToOcr) {
    ocr_.lines = {line(20, 20, 300, 44, "こんにちは")};
    run(job(640, 480));
    EXPECT_EQ(ocr_.lastSize, (SizeI{640, 480}));
}

}  // namespace
}  // namespace tmw::core
