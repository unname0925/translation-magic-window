// manga-ocr 不需要模型的部分（M2-03）：後處理、轉回文字、逐字解碼的迴圈。
// 後處理的預期值是用參考實作（tools/eval/manga_onnx.py 的 post_process，內部是 jaconv）
// 實際跑出來的，不是照規格推的。
#include "ocr/manga_ocr.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace tmw::ocr {
namespace {

TEST(MangaOcrPostProcessTest, MatchesTheReference) {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"ABC 123", "ＡＢＣ１２３"},
        {"え…えーと", "え．．．えーと"},
        {"あ・・・い", "あ．．．い"},
        {"a..b...c", "ａ．．ｂ．．．ｃ"},
        {"~^\\\"'", "～＾＼＂＇"},
        {"全角　スペース\t改行\n", "全角スペース改行"},
        {"・", "・"},
        {"..", "．．"},
        {"1%", "１％"},
        {"<unused0>", "＜ｕｎｕｓｅｄ０＞"},
    };
    for (const auto& [input, expected] : cases) {
        EXPECT_EQ(mangaOcrPostProcess(input), expected) << input;
    }
}

TEST(MangaOcrPostProcessTest, ASingleDotStaysAsItIs) {
    // 只有兩個以上連續的才換；單獨一個「.」照樣轉全形，單獨一個「・」不動
    EXPECT_EQ(mangaOcrPostProcess("a.b"), "ａ．ｂ");
    EXPECT_EQ(mangaOcrPostProcess("あ・い"), "あ・い");
}

TEST(MangaOcrDetokenizeTest, SkipsOnlyTheFiveSpecialTokens) {
    const std::vector<std::string> vocab = {"[PAD]",     "[UNK]", "[CLS]", "[SEP]", "[MASK]",
                                            "<unused0>", "ů",     "ź",     "Ż"};
    // 參考實作：[CLS] ů ź <unused0> Ż [SEP] → "ůź＜ｕｎｕｓｅｄ０＞Ż"
    const std::vector<std::int64_t> tokens = {2, 6, 7, 5, 8, 3};
    EXPECT_EQ(mangaOcrDetokenize(tokens, vocab), "ůź＜ｕｎｕｓｅｄ０＞Ż")
        << "<unused0> 不是那 5 個特殊符號之一，參考實作不會略過它";
}

TEST(MangaOcrDetokenizeTest, IgnoresTokensOutsideTheVocab) {
    const std::vector<std::string> vocab = {"[PAD]", "[UNK]", "[CLS]", "[SEP]", "あ"};
    const std::vector<std::int64_t> tokens = {2, 4, 99, -1, 4, 3};
    EXPECT_EQ(mangaOcrDetokenize(tokens, vocab), "ああ");
}

// 假的單步解碼：依序回傳預先準備的 logits，並記下每一步收到的字和位置
struct ScriptedStep {
    std::vector<std::vector<float>> logits;
    std::vector<std::pair<std::int64_t, std::int64_t>> calls;

    MangaOcrStep function() {
        return [this](std::int64_t token, std::int64_t position) -> std::span<const float> {
            calls.emplace_back(token, position);
            return logits[calls.size() - 1];
        };
    }
};

TEST(MangaOcrGreedyDecodeTest, TakesTheLargestAndStopsAtTheEnd) {
    ScriptedStep step;
    step.logits = {{0.1f, 0.9f, 0.0f, 0.2f}, {0.0f, 0.0f, 0.5f, 0.1f}, {0.0f, 0.0f, 0.0f, 3.0f}};
    const auto tokens = mangaOcrGreedyDecode(step.function(), /*start=*/0, /*eos=*/3, 300);
    EXPECT_EQ(tokens, (std::vector<std::int64_t>{0, 1, 2, 3}));
    // 每一步交出去的是上一個字和它的位置（有 KV cache，所以只要最後一個字）
    EXPECT_EQ(step.calls,
              (std::vector<std::pair<std::int64_t, std::int64_t>>{{0, 0}, {1, 1}, {2, 2}}));
}

TEST(MangaOcrGreedyDecodeTest, StopsAtTheLengthLimit) {
    // 一直重複同一個字、永遠不產生結尾：到上限就停（總長度含開頭的字）
    ScriptedStep step;
    step.logits.assign(10, {0.0f, 1.0f, 0.0f, 0.0f});
    const auto tokens = mangaOcrGreedyDecode(step.function(), 0, 3, 5);
    EXPECT_EQ(tokens, (std::vector<std::int64_t>{0, 1, 1, 1, 1}));
}

TEST(MangaOcrGreedyDecodeTest, TiesGoToTheSmallerToken) {
    // 和 numpy 的 argmax 一樣
    ScriptedStep step;
    step.logits = {{0.0f, 0.7f, 0.7f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    const auto tokens = mangaOcrGreedyDecode(step.function(), 0, 3, 300);
    EXPECT_EQ(tokens[1], 1);
}

}  // namespace
}  // namespace tmw::ocr
