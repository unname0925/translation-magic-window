// 除錯覆蓋框要畫什麼（M1-14）。
#include "core/debug_overlay.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace tmw::core {
namespace {

PipelineResult aResult() {
    PipelineResult result;
    result.region = {100, 200, 400, 500};  // 擷取當時透鏡在螢幕上的位置
    result.language = Language::Japanese;
    result.timings = {.ocrMs = 12.4, .layoutMs = 1.6, .translationMs = 300.0};

    OcrLine line;
    line.rect = {10, 20, 60, 40};
    line.text = "今日は";
    result.lines = {line};

    TranslatedBlock group;
    group.block.rect = {8, 18, 62, 42};
    group.block.text = "今日は";
    group.translation = "今天";
    result.groups = {group};
    return result;
}

TEST(DebugOverlayTest, DrawsALineAndABlock) {
    const DebugOverlay overlay = buildDebugOverlay(aResult(), {100, 200, 400, 500}, "顯示中");
    ASSERT_EQ(overlay.boxes.size(), 2u);
    EXPECT_EQ(overlay.boxes[0].rect, (RectI{10, 20, 60, 40}));
    EXPECT_EQ(overlay.boxes[0].color, kOverlayLineColor);
    EXPECT_EQ(overlay.boxes[1].rect, (RectI{8, 18, 62, 42})) << "段落的框";
    EXPECT_EQ(overlay.boxes[1].color, kOverlayBlockColor);
    EXPECT_GT(overlay.boxes[1].thickness, overlay.boxes[0].thickness) << "段落要比行明顯";
}

TEST(DebugOverlayTest, FollowsTheLensWhenItMovesAfterwards) {
    // 處理完之後把透鏡往右下拖 30、40，框要留在它原本對應的內容上
    const DebugOverlay overlay = buildDebugOverlay(aResult(), {130, 240, 430, 540}, "顯示中");
    ASSERT_FALSE(overlay.boxes.empty());
    EXPECT_EQ(overlay.boxes[0].rect, (RectI{-20, -20, 30, 0}));
}

TEST(DebugOverlayTest, SaysHowManyLinesBecameHowManyBlocks) {
    const DebugOverlay overlay = buildDebugOverlay(aResult(), {100, 200, 400, 500}, "顯示中");
    const std::string joined = [&overlay] {
        std::string out;
        for (const std::string& row : overlay.status) {
            out += row + "\n";
        }
        return out;
    }();
    EXPECT_NE(joined.find("狀態：顯示中"), std::string::npos);
    EXPECT_NE(joined.find("1 行 → 1 段（ja）"), std::string::npos);
    EXPECT_NE(joined.find("OCR 12 ms"), std::string::npos);
    EXPECT_NE(joined.find("共 314 ms"), std::string::npos);
}

TEST(DebugOverlayTest, SaysWhenNothingChanged) {
    PipelineResult result = aResult();
    result.unchanged = true;
    const DebugOverlay overlay = buildDebugOverlay(result, {100, 200, 400, 500}, "顯示中");
    bool found = false;
    for (const std::string& row : overlay.status) {
        found = found || row == "和上一次一樣";
    }
    EXPECT_TRUE(found);
}

TEST(DebugOverlayTest, SaysWhyTheTranslationFailed) {
    PipelineResult result = aResult();
    result.error = "所有引擎都暫停中";
    const DebugOverlay overlay = buildDebugOverlay(result, {100, 200, 400, 500}, "顯示中");
    EXPECT_EQ(overlay.status.back(), "翻譯失敗：所有引擎都暫停中");
}

// 畫出來的框：2×2 的小框應該四個角都有顏色，中間是透明的
TEST(RenderDebugOverlayBoxesTest, DrawsTheEdgesAndLeavesTheMiddleClear) {
    DebugOverlay overlay;
    overlay.boxes = {{RectI{1, 1, 5, 5}, Rgba{255, 0, 0, 255}, 1}};
    std::vector<std::uint32_t> pixels(6 * 6, 0xDEADBEEFu);
    renderDebugOverlayBoxes(overlay, SizeI{6, 6}, pixels);

    const auto at = [&pixels](int x, int y) { return pixels[static_cast<std::size_t>(y) * 6 + x]; };
    EXPECT_EQ(at(1, 1), 0xFFFF0000u) << "左上角";
    EXPECT_EQ(at(4, 4), 0xFFFF0000u) << "右下角（right/bottom 不含）";
    EXPECT_EQ(at(2, 2), 0u) << "框的中間要看得到底下的畫面";
    EXPECT_EQ(at(0, 0), 0u) << "框外面";
    EXPECT_EQ(at(5, 5), 0u);
}

TEST(RenderDebugOverlayBoxesTest, PremultipliesTheAlpha) {
    DebugOverlay overlay;
    overlay.boxes = {{RectI{0, 0, 2, 2}, Rgba{255, 255, 255, 128}, 1}};
    std::vector<std::uint32_t> pixels(2 * 2, 0u);
    renderDebugOverlayBoxes(overlay, SizeI{2, 2}, pixels);
    EXPECT_EQ(pixels[0], 0x80808080u) << "UpdateLayeredWindow 要的是預乘過的顏色";
}

TEST(RenderDebugOverlayBoxesTest, ClipsBoxesThatStickOut) {
    // 透鏡處理完之後被縮小，舊的框會超出範圍；不能寫到緩衝區外面
    DebugOverlay overlay;
    overlay.boxes = {{RectI{-10, -10, 100, 100}, Rgba{255, 0, 0, 255}, 3}};
    std::vector<std::uint32_t> pixels(4 * 4, 0u);
    EXPECT_NO_THROW(renderDebugOverlayBoxes(overlay, SizeI{4, 4}, pixels));
}

TEST(RenderDebugOverlayBoxesTest, RefusesABufferOfTheWrongSize) {
    std::vector<std::uint32_t> pixels(3, 0u);
    EXPECT_THROW(renderDebugOverlayBoxes(DebugOverlay{}, SizeI{4, 4}, pixels),
                 std::invalid_argument);
}

TEST(DebugOverlayTest, ShowsTheStateBeforeAnythingHasBeenProcessed) {
    const DebugOverlay overlay = buildDebugOverlay("拖動中");
    EXPECT_TRUE(overlay.boxes.empty());
    ASSERT_FALSE(overlay.status.empty());
    EXPECT_EQ(overlay.status[0], "狀態：拖動中");
}

}  // namespace
}  // namespace tmw::core
