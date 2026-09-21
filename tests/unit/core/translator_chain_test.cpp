// UT-06：用假引擎依序模擬失敗，檢查是否改用下一個；連續失敗 3 次後暫停 5 分鐘（用假時鐘）。
#include "core/translator_chain.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "support/fake_clock.h"

namespace tmw::core {
namespace {

using Strings = std::vector<std::string>;

// 假引擎：可以指定成功或失敗，並記下被呼叫幾次
class FakeTranslator final : public ITranslator {
public:
    FakeTranslator(std::string id, std::string prefix)
        : id_(std::move(id)), prefix_(std::move(prefix)) {}

    std::string id() const override { return id_; }
    bool supportsBatch() const override { return true; }

    std::vector<std::string> translate(std::span<const std::string> segments,
                                       const TranslateRequest&, std::stop_token) override {
        ++calls;
        if (failure) {
            throw TranslatorError(*failure, "假引擎故意失敗");
        }
        if (dropOneSegment) {
            return Strings(segments.size() - 1, prefix_);
        }
        std::vector<std::string> out;
        out.reserve(segments.size());
        for (const std::string& segment : segments) {
            out.push_back(prefix_ + segment);
        }
        return out;
    }

    int calls = 0;
    std::optional<TranslateError> failure;
    bool dropOneSegment = false;

private:
    std::string id_;
    std::string prefix_;
};

class TranslatorChainTest : public ::testing::Test {
protected:
    std::shared_ptr<FakeTranslator> first_ = std::make_shared<FakeTranslator>("first", "1:");
    std::shared_ptr<FakeTranslator> second_ = std::make_shared<FakeTranslator>("second", "2:");
    test::FakeClock clock_;

    TranslatorChain makeChain(ChainOptions options = {}) {
        return TranslatorChain({first_, second_}, clock_, options);
    }

    ChainResult translate(TranslatorChain& chain, const Strings& segments = {"あ"}) {
        return chain.translate(segments, TranslateRequest{"ja", "zh-TW", {}, {}},
                               std::stop_token{});
    }
};

TEST_F(TranslatorChainTest, UsesTheFirstEngineThatWorks) {
    TranslatorChain chain = makeChain();
    const ChainResult result = translate(chain);
    EXPECT_EQ(result.engine, "first");
    EXPECT_EQ(result.translations, (Strings{"1:あ"}));
    EXPECT_EQ(second_->calls, 0) << "第一個成功就不該再問第二個";
}

TEST_F(TranslatorChainTest, FallsBackToTheNextEngine) {
    first_->failure = TranslateError::Network;
    TranslatorChain chain = makeChain();
    const ChainResult result = translate(chain);
    EXPECT_EQ(result.engine, "second");
    EXPECT_EQ(result.translations, (Strings{"2:あ"}));
}

TEST_F(TranslatorChainTest, FallsBackWhenTheCountDoesNotMatch) {
    // 數量對不上也要換引擎（design.md 4.5 步驟 2）
    first_->dropOneSegment = true;
    TranslatorChain chain = makeChain();
    const ChainResult result = translate(chain, Strings{"あ", "い"});
    EXPECT_EQ(result.engine, "second");
}

TEST_F(TranslatorChainTest, ThrowsTheLastErrorWhenEveryEngineFails) {
    first_->failure = TranslateError::Network;
    second_->failure = TranslateError::RateLimited;
    TranslatorChain chain = makeChain();
    try {
        translate(chain);
        FAIL() << "全部失敗時應該丟例外";
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::RateLimited);
    }
}

TEST_F(TranslatorChainTest, PausesAnEngineAfterThreeFailuresInARow) {
    first_->failure = TranslateError::Network;
    TranslatorChain chain = makeChain();
    for (int i = 0; i < 3; ++i) {
        translate(chain);
    }
    EXPECT_EQ(first_->calls, 3);
    EXPECT_TRUE(chain.paused("first"));

    translate(chain);
    EXPECT_EQ(first_->calls, 3) << "暫停中就不要再浪費時間等它逾時";
}

TEST_F(TranslatorChainTest, UsesThePausedEngineAgainAfterFiveMinutes) {
    first_->failure = TranslateError::Network;
    TranslatorChain chain = makeChain();
    for (int i = 0; i < 3; ++i) {
        translate(chain);
    }
    clock_.advance(std::chrono::minutes(5) - std::chrono::seconds(1));
    EXPECT_TRUE(chain.paused("first"));
    translate(chain);
    EXPECT_EQ(first_->calls, 3);

    clock_.advance(std::chrono::seconds(2));
    EXPECT_FALSE(chain.paused("first"));
    first_->failure.reset();
    const ChainResult result = translate(chain);
    EXPECT_EQ(result.engine, "first");
    EXPECT_EQ(first_->calls, 4);
}

TEST_F(TranslatorChainTest, GivesThePausedEngineAFreshThreeChances) {
    first_->failure = TranslateError::Network;
    TranslatorChain chain = makeChain();
    for (int i = 0; i < 3; ++i) {
        translate(chain);
    }
    clock_.advance(std::chrono::minutes(6));
    translate(chain);  // 第 1 次失敗
    EXPECT_FALSE(chain.paused("first")) << "暫停結束後要重新數，不是一失敗就又停掉";
    translate(chain);  // 第 2 次
    EXPECT_FALSE(chain.paused("first"));
    translate(chain);  // 第 3 次
    EXPECT_TRUE(chain.paused("first"));
}

TEST_F(TranslatorChainTest, SuccessResetsTheFailureCount) {
    TranslatorChain chain = makeChain();
    first_->failure = TranslateError::Network;
    translate(chain);
    translate(chain);
    first_->failure.reset();
    translate(chain);  // 成功，歸零
    first_->failure = TranslateError::Network;
    translate(chain);
    translate(chain);
    EXPECT_FALSE(chain.paused("first")) << "只有連續失敗才算";
}

TEST_F(TranslatorChainTest, ReportsUnavailableWhenEveryEngineIsPaused) {
    first_->failure = TranslateError::Network;
    second_->failure = TranslateError::Network;
    TranslatorChain chain = makeChain();
    for (int i = 0; i < 3; ++i) {
        EXPECT_THROW(translate(chain), TranslatorError) << "兩個引擎都壞了，這幾次本來就會失敗";
    }
    ASSERT_TRUE(chain.paused("first"));
    ASSERT_TRUE(chain.paused("second"));
    try {
        translate(chain);
        FAIL() << "全部暫停時應該丟例外";
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::Unavailable);
    }
}

TEST_F(TranslatorChainTest, CancelStopsImmediatelyWithoutTryingOtherEngines) {
    first_->failure = TranslateError::Cancelled;
    TranslatorChain chain = makeChain();
    try {
        translate(chain);
        FAIL() << "取消時應該丟例外";
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::Cancelled);
    }
    EXPECT_EQ(second_->calls, 0) << "使用者已經取消了，不要再送給下一個引擎";
    EXPECT_FALSE(chain.paused("first"));
}

TEST_F(TranslatorChainTest, DoesNotCallEnginesWhenAlreadyCancelled) {
    std::stop_source source;
    source.request_stop();
    TranslatorChain chain = makeChain();
    EXPECT_THROW(
        chain.translate(Strings{"あ"}, TranslateRequest{"ja", "zh-TW", {}, {}}, source.get_token()),
        TranslatorError);
    EXPECT_EQ(first_->calls, 0);
}

TEST_F(TranslatorChainTest, TreatsUnexpectedExceptionsAsABadResponse) {
    // 引擎應該丟 TranslatorError，但第三方程式庫可能丟別的；不能讓它衝出引擎鏈
    class ThrowsSomethingElse final : public ITranslator {
    public:
        std::string id() const override { return "rude"; }
        bool supportsBatch() const override { return true; }
        std::vector<std::string> translate(std::span<const std::string>, const TranslateRequest&,
                                           std::stop_token) override {
            throw std::runtime_error("JSON 解析失敗");
        }
    };
    TranslatorChain chain({std::make_shared<ThrowsSomethingElse>(), second_}, clock_, {});
    const ChainResult result = translate(chain);
    EXPECT_EQ(result.engine, "second");
}

TEST_F(TranslatorChainTest, AnEmptyChainIsUnavailable) {
    TranslatorChain chain({}, clock_, {});
    try {
        translate(chain);
        FAIL() << "沒有引擎時應該丟例外";
    } catch (const TranslatorError& error) {
        EXPECT_EQ(error.kind(), TranslateError::Unavailable);
    }
    EXPECT_TRUE(chain.engineIds().empty());
}

TEST_F(TranslatorChainTest, DoesNotCallEnginesForAnEmptyRequest) {
    TranslatorChain chain = makeChain();
    const ChainResult result = translate(chain, {});
    EXPECT_TRUE(result.translations.empty());
    EXPECT_EQ(first_->calls, 0);
}

}  // namespace
}  // namespace tmw::core
