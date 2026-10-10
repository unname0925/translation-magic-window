// 把 OCR 的逐行結果整理成「一段一段」的文字區塊（見 docs/design.md 4.4）。
//
// 一個區塊就是結果視窗中的「一組」：一個對話框、一段段落。整理的步驟是
//   排出閱讀順序 → 依距離和對齊把相鄰的行合併 → 依語言把換行接起來。
//
// 這裡是純邏輯，不依賴 OpenCV 或 Windows：OCR 的結果由呼叫端轉成 OcrLine。
#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/geometry.h"
#include "core/language.h"

namespace tmw::core {

enum class Orientation {
    Horizontal,
    Vertical,
};

// 和同一個畫面的其他文字相比（classifyTextSize）
enum class TextSize {
    Small,
    Normal,
    Large,
};

// 一段 ルビ（振り仮名）：標在本文的哪幾個字上面。
// start 和 length 的單位是「字」（不是位元組）。
struct RubyAnnotation {
    int start = 0;
    int length = 0;
    std::string reading;

    friend bool operator==(const RubyAnnotation&, const RubyAnnotation&) = default;
};

// OCR 的一行
struct OcrLine {
    RectI rect;          // 畫面座標
    std::string text;    // UTF-8
    float score = 0.0f;  // 辨識分數
    Orientation orientation = Orientation::Horizontal;
    // 標在這一行上面的 ルビ（由 core/ruby.h 的 attachRuby 填入）
    std::vector<RubyAnnotation> ruby;

    friend bool operator==(const OcrLine&, const OcrLine&) = default;
};

// 合併後的一段
struct TextBlock {
    std::string text;  // 已經照語言接好換行
    RectI rect;        // 所有行的外框
    Orientation orientation = Orientation::Horizontal;
    Language language = Language::Unknown;
    float score = 0.0f;  // 各行分數的平均
    // 各行的 ルビ，位置已經換算成整段文字中的位置
    std::vector<RubyAnnotation> ruby;
    std::vector<OcrLine> lines;
    TextSize size = TextSize::Normal;  // classifyTextSize 填入
    // 漫畫對話框偵測器（comic-text-detector）框到的文字（markSoundEffects 填入）。
    // 它是專門找漫畫文字的模型，框到的幾乎都是真的對白、旁白：背景不是純色、OCR 分數不高也照樣蓋
    bool inBubble = false;
    // 看起來是擬聲字（markSoundEffects 填入）。使用者可以選擇不翻譯
    bool soundEffect = false;

    friend bool operator==(const TextBlock&, const TextBlock&) = default;
};

struct MergeOptions {
    // 行距不超過「字高 × 這個倍數」才算同一段
    double lineGapRatio = 0.8;
    // 兩行的起點或終點要對齊到「字高 × 這個倍數」以內
    double alignRatio = 0.6;
    // 字高相差超過這個倍數就不是同一段（標題和內文不該合併）
    double heightRatio = 1.5;
    // 橫排的行還要左右重疊到這個比例才算同一段（避免把並排的兩欄接在一起）
    double overlapRatio = 0.3;
    // 直排的欄距上限（字級的倍數）。比 lineGapRatio 寬：實測日文漫畫被擋下的配對，
    // 欄距中位數是字級的 1.57 倍（design.md 4.4）。
    double columnGapRatio = 1.6;
};

// 一兩個字的框接近正方形，從長寬比看不出是直排還是橫排，照著猜會把它孤立在自己一段。
// 用整頁的多數決補上：方向明確的行裡哪一種多，模稜兩可的就算哪一種。
// mergeIntoBlocks 會自動先做這一步。
void resolveAmbiguousOrientation(std::vector<OcrLine>& lines);

// 依閱讀順序排序：橫排由上到下、由左到右；直排（日文漫畫）由右到左、由上到下。
// 同一行（或同一欄）的判定會用字高當作容忍值。
void sortReadingOrder(std::vector<OcrLine>& lines);

// 把相鄰的行合併成段落。lines 不需要事先排序。
std::vector<TextBlock> mergeIntoBlocks(std::span<const OcrLine> lines,
                                       const MergeOptions& options = {});

// 有對話框的位置時（漫畫，comic-text-detector 找到的區塊，M2-02）：
// 同一個對話框裡的行就是同一段，不管它們之間的距離；不在任何對話框裡的行（擬聲詞、旁白）
// 照上面的距離規則分段。10 頁日文漫畫上，正確復原的區塊從 106 變成 112、被切開的從 13 變成 6
// （tools/eval/regroup_by_ctd.py）。bubbles 是空的時候和上面那個一樣。
std::vector<TextBlock> mergeIntoBlocks(std::span<const OcrLine> lines,
                                       std::span<const RectI> bubbles,
                                       const MergeOptions& options = {});

// 這一行屬於哪個對話框：重疊最多、而且蓋住這一行至少一半面積的那個。沒有就回傳 nullopt。
std::optional<std::size_t> bubbleOf(const RectI& line, std::span<const RectI> bubbles);

// 擬聲字的判斷：有找到對話框（漫畫）時，不在任何對話框裡、很短（字母、數字、假名、
// 漢字不超過 kSoundEffectMaxLetters 個），而且至少一半是假名的區塊。
// 假名這條擋掉漢字的標籤和旁註（家譜圖上的稱謂之類）。花俏的擬聲字常被 OCR 讀成漢字，
// 這些認不出來，但它們背景雜、分數低，本來就不會蓋上譯文，開關影響不到。
// 網頁漫畫 34 頁（有蓋上譯文的段落）：擬聲詞 13 個認出 11 個，誤判 7 個（多半是對話框外
// 很短的驚呼）；不看假名時是 9 個認出、誤判 20 個（標籤、旁註都被當成擬聲字）。
inline constexpr int kSoundEffectMaxLetters = 4;
bool looksLikeSoundEffect(const TextBlock& block, std::span<const RectI> bubbles);
// 填入 inBubble 和 soundEffect。沒有對話框時都是 false（無從判斷，全部當成一般文字）
void markSoundEffects(std::vector<TextBlock>& blocks, std::span<const RectI> bubbles);

// 依語言把多行接成一段文字：
// - 英文、韓文：用空格接；英文行尾的連字號（trans- / lation）要接回同一個字
// - 日文：直接相連
// startOffsets 不是 nullptr 時，填入每一行在結果中的起始位置（以「字」計）。
// ルビ 的位置要跟著搬，所以需要它。
// 開頭的句尾標點（。、，！？」』）等）移到最後：日文直排的句號常被 OCR 偵測成獨立的一塊、
// 排在最前面，譯文也跟著從「。」開始，直排時「。」就畫在第一個字上面。
// 「……」不搬：句子可以從刪節號開始
std::string moveLeadingClosingPunctuation(std::string_view text);

// 譯文裡留著幾個日文假名（`{本文|讀音}` 標記裡的也算：模型把原文的振り仮名照抄過來，
// 中文旁邊標著日文讀音）：翻成中文的譯文不該有假名
int kanaCount(std::string_view text);

// 很長的中文譯文裡零星留著的平假名（句尾的「か」、助詞「が」「の」：模型沒翻乾淨）拿掉。
// 漢字至少是平假名的 4 倍才拿；片假名不動（名字、擬聲字）；`{本文|讀音}` 標記裡面不動
std::string removeStrayHiragana(std::string_view text);

// 兩個以上連在一起的點（...、．．、・・・、。。）換成中文的刪節號「……」。直排時半形的點、
// 全形的句點看起來都像「。。。」
std::string normalizeEllipsis(std::string_view text);

// 英文漫畫的「Yoshimura-san」被翻成「吉村山」（san 照音翻）：名詞表裡的名字後面接著 -san，
// 譯文裡那個名字的譯名後面又是「山」，就換成「先生」。不在名詞表裡的名字判斷不了（富士山）
std::string fixHonorificSan(std::string_view source, std::string_view translation,
                            const std::map<std::string, std::string>& glossary);

// 模型把說明也一起回了（「…的繁體中文翻譯為：譯文」）：只留冒號後面的譯文
std::string stripTranslationPreamble(std::string_view text);

// 譯文裡留著幾個沒翻的英文單字（3 個字母以上；ignore 裡的不算，例如專有名詞表照原文留著的名字）
int englishWordCount(std::string_view text, const std::map<std::string, std::string>& ignore);

std::string joinLines(std::span<const std::string> lines, Language language,
                      std::vector<int>* startOffsets = nullptr);

// 區塊有沒有碰到畫面邊緣（被切掉一部分）。透鏡邊緣的殘缺句子預設不翻譯（design.md 4.4）。
bool touchesEdge(const RectI& rect, const SizeI& frame, int margin = 2);

// 去掉碰到邊緣的區塊
std::vector<TextBlock> dropEdgeBlocks(std::span<const TextBlock> blocks, const SizeI& frame,
                                      int margin = 2);

// 字級分三級（M2-17，design.md 4.7）：漫畫用大字表示驚訝、吼叫，小字是旁註和補充。
// 每個區塊的字級是它各行「垂直於書寫方向的大小」（橫排行高、直排欄寬）的中位數，
// 和畫面上所有行的中位數比：不到 smallRatio 倍是小字、超過 largeRatio 倍是大字。
//
// M2-17 用 90 張截圖正確答案的 972 個區塊量過：準確率 79.6%（日文漫畫 86%、遊戲 93～99%）。
// 網頁上的「小字」多半是選單按鈕，和整頁比沒有明顯差異，抓不到（召回 6～11%）；
// 依字數加權的中位數反而更差，所以用每行一票的中位數。
struct TextSizeOptions {
    double smallRatio = 0.7;
    double largeRatio = 1.5;
};
void classifyTextSize(std::span<TextBlock> blocks, const TextSizeOptions& options = {});

}  // namespace tmw::core
