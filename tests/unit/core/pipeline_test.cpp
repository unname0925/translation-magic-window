// 處理管線：OCR → 合併段落 → 判斷語言 → 翻譯，以及取消和失敗的處理。
#include "core/pipeline.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
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
    OcrResult recognize(const ImageBgra& frame, Language script, std::stop_token cancel) override {
        ++calls;
        lastSize = SizeI{frame.width, frame.height};
        askedWith.push_back(script);
        if (cancel.stop_requested()) {
            return {};
        }
        return {lines, reports, bubbles};
    }

    OcrResult recognizeManga(const ImageBgra& frame, Language script,
                             std::stop_token cancel) override {
        ++mangaCalls;
        return recognize(frame, script, cancel);
    }

    std::vector<std::optional<std::string>> reread(const ImageBgra&,
                                                   std::span<const RereadRequest> requests,
                                                   std::stop_token) override {
        ++rereadCalls;
        for (const RereadRequest& request : requests) {
            rereadRects.push_back(request.rect);
            lastLimit = request.maxCharacters;
        }
        return std::vector<std::optional<std::string>>(requests.size(), rereadText);
    }

    std::vector<OcrLine> lines;
    // 假裝是用這個模型讀出來的（Pipeline 會記下來，下次沿用）
    Language reports = Language::Japanese;
    // 漫畫模式時 OCR 會回報的對話框
    std::vector<RectI> bubbles;
    int calls = 0;
    int mangaCalls = 0;  // 其中有幾次是「一律當漫畫」（recognizeManga）
    SizeI lastSize;
    // 每次被呼叫時，呼叫端說「上次是哪個語言」
    std::vector<Language> askedWith;
    // manga-ocr 重讀：回傳什麼、被要求重讀了哪些範圍
    std::optional<std::string> rereadText;
    std::vector<RectI> rereadRects;
    int rereadCalls = 0;  // 一次 Pipeline::run 最多呼叫一次（整頁一起讀）
    int lastLimit = 0;
};

OcrLine column(int left, int top, int right, int bottom, std::string text) {
    return OcrLine{RectI{left, top, right, bottom}, std::move(text), 0.9f, Orientation::Vertical};
}

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

TEST_F(PipelineTest, PlansWhereTheTranslationsAreDrawn) {
    ocr_.lines = {line(20, 20, 200, 44, "Hello there"), line(20, 150, 200, 174, "Another block")};
    const PipelineResult result = run(job());
    ASSERT_EQ(result.overlay.size(), 2u) << "畫面只在處理管線裡，背景色要在這裡先取好";
    EXPECT_EQ(result.overlay[0].text, "譯:Hello there");
    EXPECT_EQ(result.overlay[0].background, (Rgba{0, 0, 0, 255})) << "測試畫面是全黑的";
    EXPECT_EQ(result.overlay[0].foreground, (Rgba{255, 255, 255, 255}));
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

TEST_F(PipelineTest, SendsOnlyTheGlossaryWordsOnScreen) {
    // M2-09：整張表可能有幾百個詞，只送這個畫面用得到的
    ocr_.lines = {line(20, 20, 300, 44, "悠真、逃げろ！")};
    PipelineJob request = job();
    request.glossary =
        std::make_shared<const Glossary>(Glossary{{"悠真", "悠真"}, {"魔王", "魔王"}});
    run(request);
    ASSERT_EQ(engine_->requests.size(), 1u);
    EXPECT_EQ(engine_->requests[0].glossary, (Glossary{{"悠真", "悠真"}}));
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

TEST_F(PipelineTest, OrdinaryFuriganaIsNotSentAsAMarker) {
    // M2-14：一般讀音的標記會害本機 hy-mt2 整句不翻，送出去的只有本文
    ocr_.lines = {OcrLine{RectI{200, 50, 240, 170}, "東京に行く", 0.9f, Orientation::Vertical, {}},
                  OcrLine{RectI{238, 50, 256, 98}, "とうきょう", 0.9f, Orientation::Vertical, {}}};
    const PipelineResult result = run(job());

    ASSERT_EQ(engine_->batches.size(), 1u);
    EXPECT_EQ(engine_->batches[0][0], "東京に行く");
    ASSERT_EQ(result.groups[0].block.ruby.size(), 1u) << "ルビ 本身還是留著，結果視窗要顯示";
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

// M2-04：語言判斷的結果沿用到透鏡移動或畫面換語言為止（design.md 4.4）
TEST_F(PipelineTest, AsksTheOcrToDecideTheFirstTime) {
    ocr_.lines = {line(20, 20, 200, 44, "こんにちは")};
    ocr_.reports = Language::Japanese;
    run(job());
    ASSERT_EQ(ocr_.askedWith.size(), 1u);
    EXPECT_EQ(ocr_.askedWith[0], Language::Unknown) << "第一次沒有前例，要請它自己判斷";
}

TEST_F(PipelineTest, ReusesTheLanguageItAlreadyDecided) {
    ocr_.lines = {line(20, 20, 200, 44, "こんにちは")};
    ocr_.reports = Language::Japanese;
    run(job());
    ocr_.lines = {line(20, 20, 200, 44, "さようなら")};
    run(job());
    ASSERT_EQ(ocr_.askedWith.size(), 2u);
    EXPECT_EQ(ocr_.askedWith[1], Language::Japanese) << "第二次要沿用，否則每次都要跑兩個辨識模型";
}

TEST_F(PipelineTest, ForgettingTheLensAlsoForgetsTheLanguage) {
    ocr_.lines = {line(20, 20, 200, 44, "こんにちは")};
    ocr_.reports = Language::Japanese;
    run(job());
    pipeline_.forget(1);  // 透鏡被拖到別的地方
    run(job());
    ASSERT_EQ(ocr_.askedWith.size(), 2u);
    EXPECT_EQ(ocr_.askedWith[1], Language::Unknown) << "換地方了，要重新判斷";
}

TEST_F(PipelineTest, ADeadEndLanguageIsForgottenSoItCanSwitch) {
    // 日文漫畫看到一半換成韓文條漫：日文模型讀韓文只讀得出空字串和網址，
    // 沒忘掉的話會一直用錯的模型，永遠翻不出東西
    ocr_.lines = {line(20, 20, 200, 44, "こんにちは")};
    ocr_.reports = Language::Japanese;
    run(job());
    // 韓文頁面上的網址浮水印：日文模型讀得到它，卻讀不到任何假名或漢字
    ocr_.lines = {line(20, 20, 200, 44, "novelagit.xyz")};
    run(job());
    ASSERT_EQ(ocr_.askedWith.size(), 2u);
    EXPECT_EQ(ocr_.askedWith[1], Language::Japanese) << "這一次還是沿用，換不換要看它讀到什麼";

    run(job());
    ASSERT_EQ(ocr_.askedWith.size(), 3u);
    EXPECT_EQ(ocr_.askedWith[2], Language::Unknown)
        << "日文判定卻讀不到假名或漢字，要重新判斷——不然那串網址會讓它永遠切不到韓文";
}

TEST_F(PipelineTest, AnEnglishDecisionIsNotThrownAwayForHavingNoKana) {
    // 英文頁面本來就沒有假名和漢字。拿日文的標準去檢查它，會變成每次都跑兩個模型。
    ocr_.lines = {line(20, 20, 200, 44, "Hello there")};
    ocr_.reports = Language::English;
    run(job());
    run(job());
    ASSERT_EQ(ocr_.askedWith.size(), 2u);
    EXPECT_EQ(ocr_.askedWith[1], Language::English);
}

TEST_F(PipelineTest, AKoreanDecisionIsForgottenWhenNoHangulComesBack) {
    ocr_.lines = {line(20, 20, 200, 44, "요건 어때?")};
    ocr_.reports = Language::Korean;
    run(job());
    ocr_.lines = {line(20, 20, 200, 44, "こんにちは")};
    run(job());
    run(job());
    ASSERT_EQ(ocr_.askedWith.size(), 3u);
    EXPECT_EQ(ocr_.askedWith[2], Language::Unknown) << "韓文模型讀不到韓文字母了，換回去";
}

TEST_F(PipelineTest, AChosenLanguageGoesStraightToThatModel) {
    // 設定裡指定了韓文：第一次就只用韓文模型，不必先判斷
    ocr_.lines = {line(20, 20, 200, 44, "요건 어때?")};
    ocr_.reports = Language::Korean;
    PipelineJob request = job();
    request.language = "ko";
    pipeline_.run(request, std::stop_token{});
    ASSERT_EQ(ocr_.askedWith.size(), 1u);
    EXPECT_EQ(ocr_.askedWith[0], Language::Korean);
}

TEST_F(PipelineTest, AChosenLanguageIsKeptEvenWhenItReadsNothing) {
    // 使用者說是日文就是日文。讀不到假名時自動模式會重新判斷，指定時不會。
    ocr_.lines = {line(20, 20, 200, 44, "novelagit.xyz")};
    PipelineJob request = job();
    request.language = "ja";
    pipeline_.run(request, std::stop_token{});
    pipeline_.run(request, std::stop_token{});
    ASSERT_EQ(ocr_.askedWith.size(), 2u);
    EXPECT_EQ(ocr_.askedWith[1], Language::Japanese);
}

TEST_F(PipelineTest, SwitchingBackToAutomaticDecidesAfresh) {
    // 指定語言的期間不記判斷結果，所以改回自動時這個透鏡等於沒判斷過，從頭判斷
    ocr_.lines = {line(20, 20, 200, 44, "요건 어때?")};
    ocr_.reports = Language::Korean;
    PipelineJob request = job();
    request.language = "ko";
    pipeline_.run(request, std::stop_token{});
    run(job());
    ASSERT_EQ(ocr_.askedWith.size(), 2u);
    EXPECT_EQ(ocr_.askedWith[1], Language::Unknown);
}

// M2-02：漫畫模式時，同一個對話框裡的行就是同一段
TEST_F(PipelineTest, GroupsByTheBubblesTheOcrReports) {
    // 兩行隔得很遠，只看距離會是兩段
    ocr_.lines = {line(20, 20, 200, 44, "Hello there"), line(20, 150, 200, 174, "friend")};
    ASSERT_EQ(run(job()).groups.size(), 2u) << "前提：沒有對話框時是兩段";

    pipeline_.forget(1);
    ocr_.bubbles = {RectI{10, 10, 220, 190}};
    const PipelineResult result = run(job());
    ASSERT_EQ(result.groups.size(), 1u) << "它們在同一個對話框裡";
    EXPECT_EQ(result.groups[0].block.text, "Hello there friend");
}

// 速度優化 4：畫面還在等穩定時先做 OCR，穩定之後直接沿用
class PreparedOcrTest : public PipelineTest {
protected:
    void SetUp() override { ocr_.lines = {line(20, 20, 200, 44, "Hello there")}; }

    PipelineJob preparing(std::uint64_t ticket) {
        PipelineJob out = job();
        out.prepareTicket = ticket;
        return out;
    }

    PipelineJob using_(std::uint64_t ticket) {
        PipelineJob out = job();
        out.usePrepared = ticket;
        return out;
    }
};

TEST_F(PreparedOcrTest, PreparingOnlyRunsTheOcr) {
    const PipelineResult result = run(preparing(3));
    EXPECT_EQ(ocr_.calls, 1);
    EXPECT_TRUE(result.groups.empty());
    EXPECT_TRUE(engine_->batches.empty()) << "畫面還沒穩定，不能送去翻譯（付費引擎會多花錢）";
}

TEST_F(PreparedOcrTest, TheRealRunReusesItWhenTheTicketMatches) {
    run(preparing(3));
    const PipelineResult result = run(using_(3));
    EXPECT_EQ(ocr_.calls, 1) << "OCR 只做一次";
    EXPECT_TRUE(result.timings.ocrPrepared);
    ASSERT_EQ(result.groups.size(), 1u);
    EXPECT_EQ(result.groups[0].translation, "譯:Hello there");
    EXPECT_FALSE(result.unchanged) << "預先做的那次不能記原文，否則第一次就被當成「沒變」";
}

TEST_F(PreparedOcrTest, AnotherTicketMeansTheScreenChanged) {
    run(preparing(3));
    const PipelineResult result = run(using_(4));
    EXPECT_EQ(ocr_.calls, 2);
    EXPECT_FALSE(result.timings.ocrPrepared);
}

TEST_F(PreparedOcrTest, ItIsUsedOnlyOnce) {
    run(preparing(3));
    run(using_(3));
    const PipelineResult again = run(using_(3));
    EXPECT_EQ(ocr_.calls, 2);
    EXPECT_FALSE(again.timings.ocrPrepared);
}

TEST_F(PreparedOcrTest, NotReusedAfterTheLanguageSettingChanged) {
    run(preparing(3));
    PipelineJob real = using_(3);
    real.language = "en";
    run(real);
    EXPECT_EQ(ocr_.calls, 2);
}

TEST_F(PreparedOcrTest, MangaOcrIsAlsoDoneWhilePreparing) {
    ocr_.lines = {column(300, 100, 330, 250, "楓林女子校は")};
    ocr_.bubbles = {RectI{250, 90, 340, 260}};
    ocr_.rereadText = "この楓林女子校は";
    run(preparing(3));
    EXPECT_EQ(ocr_.rereadCalls, 1);
    const PipelineResult result = run(using_(3));
    EXPECT_EQ(ocr_.rereadCalls, 1) << "正式處理時沿用預先重讀的結果";
    ASSERT_EQ(result.groups.size(), 1u);
    EXPECT_EQ(result.groups[0].block.text, "この楓林女子校は");
}

// M2-03：漫畫模式的直排對白換成 manga-ocr 重讀的文字
class MangaRereadTest : public PipelineTest {
protected:
    void SetUp() override {
        // 一個對話框裡的兩欄；PP-OCR 讀得不太對
        ocr_.lines = {column(300, 100, 330, 250, "楓林女子校は"),
                      column(260, 100, 290, 250, "学園併合")};
        ocr_.bubbles = {RectI{250, 90, 340, 260}};
        ocr_.rereadText = "この楓林女子校は学園併合";
    }
};

TEST_F(MangaRereadTest, ReplacesTheTextOfVerticalBlocks) {
    const PipelineResult result = run(job(400, 300));
    ASSERT_EQ(result.groups.size(), 1u);
    EXPECT_EQ(result.groups[0].block.text, "この楓林女子校は学園併合");
    ASSERT_EQ(ocr_.rereadRects.size(), 1u);
    EXPECT_EQ(ocr_.rereadRects[0], result.groups[0].block.rect) << "重讀的是那一段的範圍";
    EXPECT_EQ(ocr_.lastLimit, 2 * 10 + 16) << "上限是 PP-OCR 讀到的字數兩倍加 16";
}

TEST_F(MangaRereadTest, CarriesTheRubyOverToTheNewText) {
    // 「楓林女子校は」一欄 6 個字、每字 25 像素；「楓林」旁邊（右側）有 ルビ「ふうりん」，
    // 照正常流程由 attachRuby 附上去（它會覆蓋行上原本的 ruby 欄位，所以不能直接填）
    ocr_.lines.push_back(column(331, 100, 340, 150, "ふうりん"));
    const PipelineResult result = run(job(400, 300));
    ASSERT_EQ(result.groups.size(), 1u);
    ASSERT_EQ(result.groups[0].block.ruby.size(), 1u);
    EXPECT_EQ(result.groups[0].block.ruby[0].start, 2) << "「楓林」在新的文字裡往後移了兩個字";
    EXPECT_EQ(result.groups[0].block.ruby[0].reading, "ふうりん");
}

TEST_F(MangaRereadTest, OnlyInMangaMode) {
    ocr_.bubbles.clear();  // 沒有對話框：不是漫畫模式
    const PipelineResult result = run(job(400, 300));
    EXPECT_TRUE(ocr_.rereadRects.empty());
    ASSERT_FALSE(result.groups.empty());
    EXPECT_NE(result.groups[0].block.text, "この楓林女子校は学園併合");
}

TEST_F(MangaRereadTest, NotForHorizontalText) {
    ocr_.lines = {line(20, 20, 200, 44, "横書きの文")};
    ocr_.bubbles = {RectI{10, 10, 220, 60}};
    run(job(400, 300));
    EXPECT_TRUE(ocr_.rereadRects.empty());
}

TEST_F(MangaRereadTest, NotForAVeryTallNarrowLine) {
    // 頁面邊緣的註解（M0-11：27×747 像素），縮成 224×224 讀不出來
    ocr_.lines = {column(300, 10, 327, 290, "この物語はフィクションです")};
    ocr_.bubbles = {RectI{295, 5, 335, 295}};
    run(job(400, 300));
    EXPECT_TRUE(ocr_.rereadRects.empty());
}

TEST_F(MangaRereadTest, ReusesTheResultWhenNothingChanged) {
    // 畫面沒變：每 100 毫秒檢查一次，不能每次都重讀
    run(job(400, 300));
    run(job(400, 300));
    EXPECT_EQ(ocr_.rereadRects.size(), 1u);
}

TEST_F(MangaRereadTest, ReadsAllBubblesOfThePageInOneCall) {
    // 兩個對話框：一起交給 manga-ocr，它才能一批一起讀
    ocr_.lines.push_back(column(100, 100, 130, 250, "別の吹き出し"));
    ocr_.bubbles.push_back(RectI{90, 90, 140, 260});
    const PipelineResult result = run(job(400, 300));
    EXPECT_EQ(ocr_.rereadCalls, 1);
    EXPECT_EQ(ocr_.rereadRects.size(), 2u);
    ASSERT_EQ(result.groups.size(), 2u);
    for (const auto& group : result.groups) {
        EXPECT_EQ(group.block.text, "この楓林女子校は学園併合");
    }
}

TEST_F(MangaRereadTest, OnlyNewBubblesAreReadAgain) {
    run(job(400, 300));
    // 多了一個對話框：只重讀新的那個，舊的沿用
    ocr_.lines.push_back(column(100, 100, 130, 250, "別の吹き出し"));
    ocr_.bubbles.push_back(RectI{90, 90, 140, 260});
    const PipelineResult result = run(job(400, 300));
    EXPECT_EQ(ocr_.rereadCalls, 2);
    ASSERT_EQ(ocr_.rereadRects.size(), 2u);
    EXPECT_EQ(ocr_.rereadRects[1].left, 100) << "第二次只讀新的對話框";
    EXPECT_EQ(result.groups.size(), 2u);
}

TEST_F(MangaRereadTest, AMangaJobAlwaysLooksForBubbles) {
    // 網頁漫畫：不管透鏡的漫畫模式設定
    PipelineJob web = job(400, 300);
    web.manga = true;
    run(web);
    EXPECT_EQ(ocr_.mangaCalls, 1);
    run(job(400, 300));
    EXPECT_EQ(ocr_.mangaCalls, 1) << "一般的透鏡工作照舊";
}

TEST_F(MangaRereadTest, KeepsThePpOcrTextWhenMangaOcrIsUnavailable) {
    ocr_.rereadText.reset();
    const PipelineResult result = run(job(400, 300));
    ASSERT_EQ(result.groups.size(), 1u);
    EXPECT_EQ(result.groups[0].block.text, "楓林女子校は学園併合");
}

}  // namespace
}  // namespace tmw::core
