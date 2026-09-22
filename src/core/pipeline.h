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
    PipelineTimings timings;
    // 和上一次的結果一模一樣（畫面閃了一下又回到原樣）。不必新增歷史卡片。
    bool unchanged = false;
    // 非空代表翻譯失敗。原文仍然在 groups 裡，譯文是空的。
    std::string error;

    bool empty() const { return groups.empty(); }
};

// OCR 服務。正式程式接 ocr 模組的模型，測試時換成假的。
class IOcrService {
public:
    virtual ~IOcrService() = default;

    // 回傳畫面座標（相對於 frame 左上角）的文字行。取消時可以提早回傳。
    virtual std::vector<OcrLine> recognize(const ImageBgra& frame, std::stop_token cancel) = 0;
};

struct PipelineOptions {
    MergeOptions merge;
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
    };

    LensMemory& memory(int lens);

    IOcrService& ocr_;
    TranslationService& translation_;
    PipelineOptions options_;
    std::vector<std::pair<int, LensMemory>> memories_;
};

}  // namespace tmw::core
