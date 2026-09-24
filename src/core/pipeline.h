// 處理管線：從透鏡底下的畫面走到「原文／譯文成組」的結果（見 docs/design.md 3.1、3.3）。
//
//   OCR → 排閱讀順序、合併段落 → 判斷語言 → 翻譯（查快取、引擎鏈、簡轉繁）
//
// 這個檔案只有純邏輯，OCR 透過 IOcrService 注入（實作在 ocr 模組，core 不認得 OpenCV）。
// 佇列和執行緒在 pipeline_worker.h。
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "core/geometry.h"
#include "core/image.h"
#include "core/language.h"
#include "core/ruby.h"
#include "core/text_layout.h"
#include "core/translation_service.h"

namespace tmw::core {

// 一次處理的輸入
struct PipelineJob {
    std::uint64_t generation = 0;  // 透鏡的流水號，結果要帶回去判斷是否過時
    int lens = 0;                  // 透鏡編號（未來會有多個透鏡）
    RectI region;                  // 透鏡在螢幕上的範圍，結果視窗用來標示來源
    ImageBgra frame;               // 透鏡底下的畫面
    bool manual = false;           // 快捷鍵觸發
    std::string language;          // "ja"|"en"|"ko"|"auto"；空字串等於 auto
};

struct PipelineTimings {
    double ocrMs = 0.0;
    double layoutMs = 0.0;
    double translationMs = 0.0;

    double totalMs() const { return ocrMs + layoutMs + translationMs; }
};

// 一組：一個對話框或一個段落
struct TranslatedBlock {
    TextBlock block;
    std::string translation;

    friend bool operator==(const TranslatedBlock&, const TranslatedBlock&) = default;
};

struct PipelineResult {
    std::uint64_t generation = 0;
    int lens = 0;
    RectI region;
    Language language = Language::Unknown;
    std::vector<TranslatedBlock> groups;
    // OCR 讀到的每一行，合併成段落之前的樣子。除錯覆蓋框和除錯傾印用它
    // 回答「為什麼這句被切開」（design.md 4.12）。
    std::vector<OcrLine> lines;
    PipelineTimings timings;
    // 和上一次的結果一模一樣（畫面閃了一下又回到原樣）。不必新增歷史卡片。
    bool unchanged = false;
    // 非空代表翻譯失敗。原文仍然在 groups 裡，譯文是空的。
    std::string error;

    bool empty() const { return groups.empty(); }
};

struct OcrResult {
    // 畫面座標（相對於 frame 左上角）的文字行
    std::vector<OcrLine> lines;
    // 實際採用的辨識模型（Korean，或主模型的 Japanese／English）。
    // 呼叫端記下來下次傳回去，就不用每次都判斷（design.md 4.4「語言判斷」）。
    Language script = Language::Unknown;
};

// OCR 服務。正式程式接 ocr 模組的模型，測試時換成假的。
class IOcrService {
public:
    virtual ~IOcrService() = default;

    // script：上一次判斷出來的語言，沿用它就只跑那一個辨識模型。
    // Unknown 表示「請重新判斷」——兩個模型都要跑，慢一點但會挑對。
    // 取消時可以提早回傳。
    virtual OcrResult recognize(const ImageBgra& frame, Language script,
                                std::stop_token cancel) = 0;
};

struct PipelineOptions {
    MergeOptions merge;
    RubyOptions ruby;
    // 碰到透鏡邊緣的句子被切掉了一半，翻了也沒意義（design.md 4.4）
    bool dropEdgeBlocks = true;
    int edgeMargin = 2;
    // 給 LLM 當上下文的「最近幾組」原文和譯文
    std::size_t contextGroups = 4;
};

// 一次處理。可以從任何執行緒呼叫，但同一個 Pipeline 不要同時跑兩次
// （OCR 模型的推論本來就要依序執行，避免搶 GPU）。
class Pipeline {
public:
    Pipeline(IOcrService& ocr, TranslationService& translation, PipelineOptions options = {});

    PipelineResult run(const PipelineJob& job, std::stop_token cancel);

    // 忘掉「上一次的結果」，下一次一定會被當成新的內容（例如透鏡被拖到別的地方）
    void forget(int lens);

private:
    struct LensMemory {
        std::string text;                                         // 上一次的原文（接起來）
        std::vector<std::pair<std::string, std::string>> recent;  // 最近幾組原文和譯文
        // 上一次判斷出來的語言，沿用到透鏡移動（forget）或這個模型讀不出東西為止
        Language script = Language::Unknown;
    };

    // 記住這次用的語言；但如果讀出來的內容根本不含這個模型該讀的文字
    // （例如畫面從日文換成韓文條漫，日文模型只讀得出空字串），就忘掉它重新判斷。
    void rememberScript(int lens, Language script, const std::vector<OcrLine>& lines);

    LensMemory& memory(int lens);

    IOcrService& ocr_;
    TranslationService& translation_;
    PipelineOptions options_;
    std::vector<std::pair<int, LensMemory>> memories_;
};

}  // namespace tmw::core
