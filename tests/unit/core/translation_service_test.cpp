// 翻譯服務：查快取 → 引擎鏈 → OpenCC 的整個流程（見 docs/design.md 4.5）。
#include "core/translation_service.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "support/fake_clock.h"

namespace tmw::core {
namespace {

using Strings = std::vector<std::string>;

// 假引擎：把每一段前面加上引擎名稱，並記下每一次收到哪些段落
class RecordingTranslator final : public ITranslator {
public:
    explicit RecordingTranslator(std::string id) : id_(std::move(id)) {}

    std::string id() const override { return id_; }
    bool supportsBatch() const override { return true; }

    std::vector<std::string> translate(std::span<const std::string> segments,
                                       const TranslateRequest&, std::stop_token) override {
        batches.emplace_back(segments.begin(), segments.end());
        std::vector<std::string> out;
        out.reserve(segments.size());
        for (const std::string& segment : segments) {
            out.push_back(id_ + ":" + segment);
        }
        return out;
    }

    std::vector<Strings> batches;

private:
    std::string id_;
};

// 假的簡轉繁：把「软」換成「軟」就夠了，真正的 OpenCC 有自己的測試
class FakeConverter final : public ITextConverter {
public:
    std::string convert(const std::string& text) const override {
        std::string out = text;
        for (std::size_t at = out.find("软"); at != std::string::npos; at = out.find("软", at)) {
            out.replace(at, std::string("软").size(), "軟");
        }
        return out;
    }
};

class TranslationServiceTest : public ::testing::Test {
protected:
    std::shared_ptr<RecordingTranslator> engine_ = std::make_shared<RecordingTranslator>("fake");
    test::FakeClock clock_;
    std::shared_ptr<TranslatorChain> chain_ = std::make_shared<TranslatorChain>(
        std::vector<std::shared_ptr<ITranslator>>{engine_}, clock_, ChainOptions{});
    TranslationService service_{chain_, std::make_shared<FakeConverter>()};

    Strings translate(const Strings& segments) {
        return service_.translate(segments, TranslateRequest{"ja", "zh-TW", {}, {}},
                                  std::stop_token{});
    }
};

TEST_F(TranslationServiceTest, TranslatesAndConverts) {
    EXPECT_EQ(translate({"软件", "こんにちは"}), (Strings{"fake:軟件", "fake:こんにちは"}));
}

TEST_F(TranslationServiceTest, SkipsSegmentsWithoutAnyLetters) {
    // 純數字和符號翻了也是原樣回來，不如省下時間和費用
    EXPECT_EQ(translate({"110/110", "こんにちは", "00:35"}),
              (Strings{"110/110", "fake:こんにちは", "00:35"}));
    ASSERT_EQ(engine_->batches.size(), 1u);
    EXPECT_EQ(engine_->batches[0], (Strings{"こんにちは"})) << "只有需要翻的那一段被送出";
}

TEST_F(TranslationServiceTest, UsesTheCacheOnTheSecondCall) {
    translate({"こんにちは", "さようなら"});
    const Strings again = translate({"さようなら", "こんにちは"});
    EXPECT_EQ(again, (Strings{"fake:さようなら", "fake:こんにちは"}));
    EXPECT_EQ(engine_->batches.size(), 1u) << "全部命中快取就不必再問引擎";
}

TEST_F(TranslationServiceTest, OnlySendsTheSegmentsThatChanged) {
    // 畫面捲動一點點時，多數段落和上一次相同
    translate({"一つ目", "二つ目"});
    translate({"二つ目", "三つ目"});
    ASSERT_EQ(engine_->batches.size(), 2u);
    EXPECT_EQ(engine_->batches[1], (Strings{"三つ目"}));
}

TEST_F(TranslationServiceTest, CachedTextIsAlreadyConverted) {
    translate({"软件"});
    EXPECT_EQ(translate({"软件"}), (Strings{"fake:軟件"}));
    EXPECT_EQ(engine_->batches.size(), 1u);
}

TEST_F(TranslationServiceTest, SendsRepeatedTextOnlyOnce) {
    // 同一個畫面上兩個一樣的按鈕
    const Strings out = translate({"セーブ", "セーブ ", "ロード"});
    EXPECT_EQ(out, (Strings{"fake:セーブ", "fake:セーブ", "fake:ロード"}));
    ASSERT_EQ(engine_->batches.size(), 1u);
    EXPECT_EQ(engine_->batches[0], (Strings{"セーブ", "ロード"}));
}

TEST_F(TranslationServiceTest, DoesNotCallTheEngineWhenNothingNeedsTranslating) {
    EXPECT_EQ(translate({"110/110", "", "- - -"}), (Strings{"110/110", "", "- - -"}));
    EXPECT_TRUE(engine_->batches.empty());
}

TEST_F(TranslationServiceTest, FailuresReachTheCaller) {
    class AlwaysFails final : public ITranslator {
    public:
        std::string id() const override { return "down"; }
        bool supportsBatch() const override { return true; }
        std::vector<std::string> translate(std::span<const std::string>, const TranslateRequest&,
                                           std::stop_token) override {
            throw TranslatorError(TranslateError::Network, "連不上");
        }
    };
    auto chain = std::make_shared<TranslatorChain>(
        std::vector<std::shared_ptr<ITranslator>>{std::make_shared<AlwaysFails>()}, clock_,
        ChainOptions{});
    TranslationService service(chain, std::make_shared<NullTextConverter>());
    EXPECT_THROW(service.translate(Strings{"こんにちは"}, TranslateRequest{"ja", "zh-TW", {}, {}},
                                   std::stop_token{}),
                 TranslatorError);
}

TEST(NeedsTranslationTest, OnlyTextWithLetters) {
    EXPECT_TRUE(needsTranslation("こんにちは"));
    EXPECT_TRUE(needsTranslation("SAVE"));
    EXPECT_TRUE(needsTranslation("이 대통령"));
    EXPECT_FALSE(needsTranslation(""));
    EXPECT_FALSE(needsTranslation("   "));
    EXPECT_FALSE(needsTranslation("110/110"));
    EXPECT_FALSE(needsTranslation("00:35"));
    EXPECT_FALSE(needsTranslation("→"));
}

}  // namespace
}  // namespace tmw::core
