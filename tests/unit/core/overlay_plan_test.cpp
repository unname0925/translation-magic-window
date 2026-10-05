// M3：譯文蓋在原文位置上要怎麼蓋
#include "core/overlay_plan.h"

#include <gtest/gtest.h>

#include <vector>

namespace tmw::core {
namespace {

ImageBgra solidFrame(int width, int height, Rgba color) {
    ImageBgra frame(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint8_t* p = frame.pixel(x, y);
            p[0] = color.b;
            p[1] = color.g;
            p[2] = color.r;
            p[3] = 255;
        }
    }
    return frame;
}

TranslatedBlock block(RectI rect, std::string translation, Orientation orientation) {
    TranslatedBlock out;
    out.block.rect = rect;
    out.block.orientation = orientation;
    out.block.score = 0.95f;
    out.block.text = translation;  // 原文：沒有字的段落（符號）不蓋，測試用譯文代替
    out.translation = std::move(translation);
    return out;
}

TEST(OverlayPlanTest, TheBackgroundIsTheColourAroundTheText) {
    ImageBgra frame = solidFrame(100, 100, Rgba{250, 245, 230, 255});
    // 框裡面是黑色的字，不能影響背景色
    for (int y = 40; y < 60; ++y) {
        for (int x = 40; x < 60; ++x) {
            std::uint8_t* p = frame.pixel(x, y);
            p[0] = p[1] = p[2] = 0;
        }
    }
    EXPECT_EQ(sampleBackground(frame, RectI{40, 40, 60, 60}), (Rgba{250, 245, 230, 255}));
}

TEST(OverlayPlanTest, AFewOddPixelsDoNotChangeTheBackground) {
    // 中位數：外圍一圈有幾個是別的顏色（相鄰的字、網點），不影響結果
    ImageBgra frame = solidFrame(100, 100, Rgba{255, 255, 255, 255});
    for (int x = 30; x < 35; ++x) {
        std::uint8_t* p = frame.pixel(x, 38);
        p[0] = p[1] = p[2] = 0;
    }
    EXPECT_EQ(sampleBackground(frame, RectI{40, 40, 60, 60}), (Rgba{255, 255, 255, 255}));
}

TEST(OverlayPlanTest, ABoxAtTheEdgeSamplesWhatIsInside) {
    const ImageBgra frame = solidFrame(50, 50, Rgba{10, 20, 30, 255});
    EXPECT_EQ(sampleBackground(frame, RectI{0, 0, 50, 50}), (Rgba{255, 255, 255, 255}))
        << "外圍全在畫面外：沒有可取的，用白色";
    EXPECT_EQ(sampleBackground(frame, RectI{0, 0, 20, 20}), (Rgba{10, 20, 30, 255}));
}

TEST(OverlayPlanTest, TextIsBlackOnLightAndWhiteOnDark) {
    EXPECT_EQ(readableTextColor(Rgba{255, 255, 255, 255}), (Rgba{0, 0, 0, 255}));
    EXPECT_EQ(readableTextColor(Rgba{30, 50, 120, 255}), (Rgba{255, 255, 255, 255}));
}

TEST(OverlayPlanTest, TheAuthorsReadingIsShownInParentheses) {
    const OverlayText marked = overlayText("我要{認真|來真的}打了");
    EXPECT_EQ(marked.text, "我要認真打了");
    EXPECT_EQ(marked.ruby, (std::vector<OverlayRuby>{{2, 2, "來真的"}})) << "第 3、4 個字旁邊";

    const OverlayText plain = overlayText("沒有標記");
    EXPECT_EQ(plain.text, "沒有標記");
    EXPECT_TRUE(plain.ruby.empty());
}

TEST(OverlayPlanTest, NotesFromOrdinaryEnginesBecomeRubyToo) {
    // net/ruby_notes 的格式：譯文後面接「　［原文（讀音）→ 譯文（讀音的譯文）、…］」
    const OverlayText text = overlayText(
        "我要認真打一場，宿敵。　［本気（マジ）→ 認真（玩真的）、強敵（とも）→ 宿敵（朋友）］");
    EXPECT_EQ(text.text, "我要認真打一場，宿敵。") << "註解本身不畫，會把字擠小";
    EXPECT_EQ(text.ruby, (std::vector<OverlayRuby>{{2, 2, "玩真的"}, {8, 2, "朋友"}}));
}

TEST(OverlayPlanTest, NotesThatCannotBePlacedAreDropped) {
    const OverlayText text = overlayText("我們走吧。　［本気（マジ）→ 認真（玩真的）］");
    EXPECT_EQ(text.text, "我們走吧。");
    EXPECT_TRUE(text.ruby.empty()) << "正文裡沒有「認真」：只留在結果視窗";
}

TEST(OverlayPlanTest, TextDrawnOnTheArtworkIsLeftAlone) {
    // 擬聲詞畫在圖上：外圍是黑白相間的畫，不是對話框的白底
    ImageBgra frame = solidFrame(200, 200, Rgba{255, 255, 255, 255});
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 200; ++x) {
            if (((x / 3) + (y / 3)) % 2 == 0) {
                std::uint8_t* p = frame.pixel(x, y);
                p[0] = p[1] = p[2] = 0;
            }
        }
    }
    const std::vector<TranslatedBlock> groups{
        block(RectI{50, 50, 120, 90}, "轟", Orientation::Horizontal)};
    EXPECT_LT(backgroundUniformity(frame, RectI{47, 47, 123, 93}, Rgba{255, 255, 255, 255}), 0.8);
    EXPECT_TRUE(planOverlay(frame, groups).empty()) << "純色蓋上去會是一塊突兀的色塊";
}

TEST(OverlayPlanTest, TextTheComicDetectorFoundIsCoveredOnABusyBackground) {
    // 雲朵形的對話框：框的四角切到邊線和背後的圖，量起來不是純色
    ImageBgra frame = solidFrame(200, 200, Rgba{255, 255, 255, 255});
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 200; ++x) {
            if (((x / 3) + (y / 3)) % 2 == 0) {
                std::uint8_t* p = frame.pixel(x, y);
                p[0] = p[1] = p[2] = 0;
            }
        }
    }
    TranslatedBlock line = block(RectI{50, 50, 120, 90}, "你在笑什麼", Orientation::Horizontal);
    line.block.score = 0.7f;
    ASSERT_TRUE(planOverlay(frame, std::span(&line, 1)).empty()) << "前提：沒框到時不蓋";
    line.block.inBubble = true;
    OverlayDrops drops;
    EXPECT_EQ(planOverlay(frame, std::span(&line, 1), nullptr, &drops).size(), 1u);
    EXPECT_EQ(drops.total(), 0);
    line.block.score = 0.2f;
    EXPECT_TRUE(planOverlay(frame, std::span(&line, 1), nullptr, &drops).empty());
    EXPECT_EQ(drops.lowScore, 1) << "框到了，分數太低的照樣不蓋";
}

TEST(OverlayPlanTest, TranslucentGameTextBoxesAreCoveredWhenTheTextIsClear) {
    // 半透明的對話框：很暗，但透出底下的畫面，顏色有一點起伏
    ImageBgra frame = solidFrame(200, 200, Rgba{15, 12, 10, 255});
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 200; ++x) {
            if ((x * 7 + y * 13) % 3 == 0) {
                std::uint8_t* p = frame.pixel(x, y);
                p[0] = p[1] = p[2] = 55;
            }
        }
    }
    TranslatedBlock line = block(RectI{40, 80, 160, 110}, "要逃跑嗎", Orientation::Horizontal);
    EXPECT_EQ(planOverlay(frame, std::span(&line, 1)).size(), 1u);
    line.block.score = 0.7f;
    EXPECT_TRUE(planOverlay(frame, std::span(&line, 1)).empty())
        << "OCR 沒什麼把握的，可能是畫在圖上的擬聲詞";
}

TEST(OverlayPlanTest, IconsReadAsTextAreLeftAlone) {
    const ImageBgra frame = solidFrame(200, 200, Rgba{255, 255, 255, 255});
    TranslatedBlock icon = block(RectI{40, 40, 80, 80}, "旅行", Orientation::Horizontal);
    icon.block.score = 0.25f;
    EXPECT_TRUE(planOverlay(frame, std::span(&icon, 1)).empty());
}

TEST(OverlayPlanTest, TheTranslationKeepsTheOriginalTextColour) {
    // M4-02：深藍底上的黃字，譯文也是黃字（不是一律白字）
    ImageBgra frame = solidFrame(200, 100, Rgba{20, 30, 70, 255});
    for (int y = 30; y < 70; ++y) {
        for (int x = 40; x < 160; ++x) {
            if ((x / 8) % 2 == 0) {  // 一條條粗筆畫
                std::uint8_t* p = frame.pixel(x, y);
                p[0] = 40;
                p[1] = 210;
                p[2] = 250;
            }
        }
    }
    const TranslatedBlock name = block(RectI{40, 30, 160, 70}, "立花", Orientation::Horizontal);
    const std::vector<OverlayItem> items = planOverlay(frame, std::span(&name, 1));
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].foreground, (Rgba{250, 210, 40, 255}));
    EXPECT_FALSE(items[0].outline.has_value());
}

// 假的背景修補：記下被叫了幾次，回傳一塊灰色
class FakeInpainter final : public IInpainter {
public:
    std::optional<ImageBgra> inpaint(const ImageBgra& frame, const RectI& rect) override {
        (void)frame;
        ++calls;
        if (fail) {
            return std::nullopt;
        }
        return solidFrame(rect.width(), rect.height(), Rgba{90, 90, 90, 255});
    }
    int calls = 0;
    bool fail = false;
};

// 黑白相間的畫：外圍不是純色
ImageBgra busyFrame() {
    ImageBgra frame = solidFrame(200, 200, Rgba{255, 255, 255, 255});
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 200; ++x) {
            if (((x / 3) + (y / 3)) % 2 == 0) {
                std::uint8_t* p = frame.pixel(x, y);
                p[0] = p[1] = p[2] = 0;
            }
        }
    }
    return frame;
}

TEST(OverlayPlanTest, TextOnPicturesIsInpaintedWhenPossible) {
    // M4-01：背景是畫、OCR 很有把握的一句話：抹掉原文補成周圍的樣子，再畫譯文
    const ImageBgra frame = busyFrame();
    FakeInpainter inpainter;
    const TranslatedBlock line =
        block(RectI{40, 80, 160, 110}, "要逃跑嗎", Orientation::Horizontal);
    TranslatedBlock source = line;
    source.block.text = "脱走でもしたんですかね";
    const std::vector<OverlayItem> items = planOverlay(frame, std::span(&source, 1), &inpainter);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(inpainter.calls, 1);
    EXPECT_EQ(items[0].patch.width, items[0].rect.width()) << "補出來的圖和要蓋的範圍一樣大";
    EXPECT_TRUE(items[0].outline.has_value()) << "畫在有圖案的背景上，加描邊才讀得清楚";

    inpainter.fail = true;
    EXPECT_TRUE(planOverlay(frame, std::span(&source, 1), &inpainter).empty())
        << "補不出來（沒有顯示卡）：和以前一樣不蓋";
}

TEST(OverlayPlanTest, SoundEffectsAndPunctuationAreNotInpainted) {
    const ImageBgra frame = busyFrame();
    FakeInpainter inpainter;
    TranslatedBlock effect = block(RectI{40, 40, 120, 120}, "沙沙", Orientation::Vertical);
    effect.block.text = "ザワザワ";
    effect.block.score = 0.55f;  // 擬聲詞：OCR 沒什麼把握
    TranslatedBlock bang = block(RectI{140, 40, 180, 120}, "！", Orientation::Vertical);
    bang.block.text = "！";
    const std::vector<TranslatedBlock> groups{effect, bang};
    EXPECT_TRUE(planOverlay(frame, groups, &inpainter).empty());
    EXPECT_EQ(inpainter.calls, 0) << "修補一次要 0.1 秒，不值得的段落連試都不試";
}

TEST(OverlayPlanTest, SymbolsReadAsTextAreLeftAlone) {
    // 把愛心、閃光讀成一兩個字：框很小（不到畫面的 0.15%），或根本沒有字
    const ImageBgra frame = solidFrame(1000, 1500, Rgba{255, 255, 255, 255});
    TranslatedBlock heart = block(RectI{100, 100, 140, 150}, "心", Orientation::Vertical);
    TranslatedBlock dots = block(RectI{300, 100, 340, 300}, "……", Orientation::Vertical);
    TranslatedBlock line = block(RectI{500, 100, 560, 400}, "你在笑什麼", Orientation::Vertical);
    TranslatedBlock shortLine = block(RectI{700, 100, 760, 200}, "欸？", Orientation::Vertical);
    const std::vector<TranslatedBlock> groups{heart, dots, line, shortLine};
    OverlayDrops drops;
    const std::vector<OverlayItem> items = planOverlay(frame, groups, nullptr, &drops);
    ASSERT_EQ(items.size(), 2u) << "一般的句子、框夠大的短句照樣蓋";
    EXPECT_EQ(drops.symbols, 2);
    EXPECT_EQ(coverDecision(frame, heart.block, false), CoverDecision::Symbols);
    EXPECT_EQ(coverDecision(frame, line.block, false), CoverDecision::Cover);
}

TEST(OverlayPlanTest, PlainBackgroundsAreNotInpainted) {
    const ImageBgra frame = solidFrame(200, 200, Rgba{255, 255, 255, 255});
    FakeInpainter inpainter;
    TranslatedBlock line = block(RectI{40, 80, 160, 110}, "你好", Orientation::Horizontal);
    line.block.text = "こんにちは";
    const std::vector<OverlayItem> items = planOverlay(frame, std::span(&line, 1), &inpainter);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_TRUE(items[0].patch.empty()) << "純色背景用純色填就好";
    EXPECT_EQ(inpainter.calls, 0);
}

TEST(OverlayPlanTest, CompositingShowsWhatIsOnScreen) {
    ImageBgra screen = solidFrame(2, 1, Rgba{200, 100, 50, 255});
    ImageBgra overlay(2, 1);  // 第一個像素透明
    std::uint8_t* covered = overlay.pixel(1, 0);
    covered[0] = 10;  // 不透明的藍黑色
    covered[1] = 20;
    covered[2] = 30;
    covered[3] = 255;
    compositeOver(screen, overlay);
    EXPECT_EQ(screen.pixel(0, 0)[2], 200) << "透明的地方看到原本的畫面";
    EXPECT_EQ(screen.pixel(1, 0)[2], 30) << "不透明的地方看到譯文";
    EXPECT_EQ(screen.pixel(1, 0)[0], 10);
}

TEST(OverlayPlanTest, PlansEachTranslatedBlock) {
    const ImageBgra frame = solidFrame(200, 200, Rgba{255, 255, 255, 255});
    const std::vector<TranslatedBlock> groups{
        block(RectI{10, 10, 40, 120}, "你要去哪裡", Orientation::Vertical),
        block(RectI{60, 10, 190, 30}, "", Orientation::Horizontal),  // 翻譯失敗
        block(RectI{60, 150, 190, 170}, "存檔", Orientation::Horizontal),
    };
    const std::vector<OverlayItem> items = planOverlay(frame, groups);
    ASSERT_EQ(items.size(), 2u) << "翻譯失敗的段落不蓋，原文照樣看得到";
    EXPECT_TRUE(items[0].vertical) << "直排的對話框，譯文也直排";
    EXPECT_EQ(items[0].rect, (RectI{7, 7, 43, 123})) << "往外多蓋一點，蓋住反鋸齒的邊";
    EXPECT_EQ(items[0].foreground, (Rgba{0, 0, 0, 255}));
    EXPECT_FALSE(items[1].vertical);
    EXPECT_EQ(items[1].text, "存檔");
}

TEST(OverlayPlanTest, RemembersHowThickTheOriginalLinesAre) {
    const ImageBgra frame = solidFrame(200, 200, Rgba{255, 255, 255, 255});
    TranslatedBlock column = block(RectI{10, 10, 70, 120}, "你要去哪裡", Orientation::Vertical);
    for (const int left : {10, 32, 50}) {
        column.block.lines.push_back(
            OcrLine{RectI{left, 10, left + 20, 120}, "", 0.9f, Orientation::Vertical});
    }
    column.block.lines[2].rect.right = 74;  // 一欄特別寬：中位數不受影響
    const std::vector<OverlayItem> items = planOverlay(frame, std::span(&column, 1));
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].lineThickness, 20) << "直排看欄寬";
}

}  // namespace
}  // namespace tmw::core
