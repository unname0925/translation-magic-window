// 處理管線：從透鏡底下的畫面走到「原文／譯文成組」的結果（見 docs/design.md 3.1、3.3）。
//
//   OCR → 排閱讀順序、合併段落 → 判斷語言 → 翻譯（查快取、引擎鏈、簡轉繁）
//
// 這個檔案只有純邏輯，OCR 透過 IOcrService 注入（實作在 ocr 模組，core 不認得 OpenCV）。
// 佇列和執行緒在 pipeline_worker.h。
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/geometry.h"
#include "core/glossary.h"
#include "core/image.h"
#include "core/inpainter.h"
#include "core/language.h"
#include "core/overlay_item.h"
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
    // "ja"|"en"|"ko"|"auto"；空字串等於 auto。指定語言時 OCR 只用那個模型、不判斷，
    // 翻譯的來源語言也固定是它（設定裡的「辨識語言」）
    std::string language;
    // 使用者的專有名詞表（glossary.txt）。沒有時是 nullptr。
    // UI 執行緒讀檔、工作執行緒只讀，所以用共享的唯讀副本，改檔時換一份新的。
    std::shared_ptr<const Glossary> glossary;
    // 背景修補（M4-01）。只有打開「在原位顯示譯文」、而且有顯示卡和模型時才有；
    // 沒有時背景不是純色的段落照舊（遊戲對話框用純色，其餘不蓋）
    std::shared_ptr<IInpainter> inpainter;
    // 不是 0：畫面還在等穩定，先做 OCR（包括 manga-ocr 重讀），結果記在這個票號下就結束，
    // 不翻譯、不記原文、也沒有結果要送回去（速度優化 4，AutoTrigger 的 onPrepare）
    std::uint64_t prepareTicket = 0;
    // 不是 0：預先做過 OCR 的票號，之後畫面沒有變。記下來的是同一個票號就直接沿用
    std::uint64_t usePrepared = 0;
    // 一律當漫畫處理（找對話框、直排對白用 manga-ocr 重讀），不管透鏡的漫畫模式設定（網頁漫畫）
    bool manga = false;
};

struct PipelineTimings {
    double ocrMs = 0.0;
    double layoutMs = 0.0;
    double translationMs = 0.0;
    // 規劃覆蓋層（M3），包括背景修補（M4-01，每塊約 0.13 秒）
    double overlayMs = 0.0;
    // 漫畫模式用 manga-ocr 重讀直排對白的時間（已經算在 ocrMs 裡，另外記下來找慢在哪）
    double rereadMs = 0.0;
    // OCR 是畫面還在等穩定時預先做好的（ocrMs 只剩沿用的時間，幾乎是 0）
    bool ocrPrepared = false;

    double totalMs() const { return ocrMs + layoutMs + translationMs + overlayMs; }
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
    // 翻譯成功，但不是首選引擎翻的：原因寫在這裡（「改用 google（…：連不上…）」）。
    // 使用者才知道譯文為什麼突然變了樣（M2-08）。
    std::string notice;
    // 譯文要怎麼蓋在原文上（M3）：座標和 groups 一樣相對於畫面左上角。
    // 背景色要從畫面取，畫面不會跟著結果帶出去，所以在這裡先規劃好。
    std::vector<OverlayItem> overlay;

    bool empty() const { return groups.empty(); }
};

struct OcrResult {
    // 畫面座標（相對於 frame 左上角）的文字行
    std::vector<OcrLine> lines;
    // 實際採用的辨識模型（Korean，或主模型的 Japanese／English）。
    // 呼叫端記下來下次傳回去，就不用每次都判斷（design.md 4.4「語言判斷」）。
    Language script = Language::Unknown;
    // 漫畫模式時 comic-text-detector 找到的對話框（畫面座標）。分段時同一個對話框裡的行
    // 就是同一段（M2-02）。不是漫畫模式時是空的。
    std::vector<RectI> bubbles;
};

// 要重讀的一段：rect 是畫面座標，maxCharacters 是最多產生幾個字
struct RereadRequest {
    RectI rect;
    int maxCharacters = 0;
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

    // 和 recognize 相同，但不管透鏡的漫畫模式設定，一律當漫畫找對話框（網頁漫畫整頁翻譯）。
    // 沒有漫畫模式的模型時和 recognize 相同
    virtual OcrResult recognizeManga(const ImageBgra& frame, Language script,
                                     std::stop_token cancel) {
        return recognize(frame, script, cancel);
    }

    // 用更準的模型重讀好幾段（漫畫模式的直排對白用 manga-ocr，M2-03）。一起交出去，
    // 模型才能一批一起讀（每一步的固定開銷只付一次）。結果的順序和 requests 相同；
    // 不支援、模型沒載入或讀不出東西的那段是 nullopt，呼叫端沿用原本的文字。
    virtual std::vector<std::optional<std::string>> reread(const ImageBgra& /*frame*/,
                                                           std::span<const RereadRequest> requests,
                                                           std::stop_token /*cancel*/) {
        return std::vector<std::optional<std::string>>(requests.size());
    }
};

// 漫畫模式下這一段要不要交給 manga-ocr 重讀（M0-11 的規則）：直排、而且高不超過寬的 10 倍。
// 極細長的一整行（頁面邊緣的註解）縮成 224×224 就讀不出來，逐字解碼還會一直重複同一個字。
bool shouldRereadWithMangaOcr(const TextBlock& block);

// manga-ocr 最多產生幾個字：PP-OCR 在同一段讀到的字數的兩倍再加 16。
// 逐字解碼偶爾會一直重複同一個字停不下來，要有上限。
int mangaOcrCharacterLimit(std::string_view ppOcrText);

struct PipelineOptions {
    MergeOptions merge;
    RubyOptions ruby;
    // 碰到透鏡邊緣的句子被切掉了一半，翻了也沒意義（design.md 4.4）
    bool dropEdgeBlocks = true;
    int edgeMargin = 2;
    // 給 LLM 當上下文的「最近幾組」原文和譯文
    std::size_t contextGroups = 4;
    // ルビ的一般讀音表（M2-13）。沒有時（讀音表還沒產生）用「讀音是片假名」判斷特殊讀音
    std::shared_ptr<const FuriganaReadings> furigana;
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
        // 上一次 manga-ocr 重讀的結果，以 PP-OCR 讀到的文字為鍵。畫面沒變時沿用，
        // 不必每 100 毫秒重讀一次，翻譯也才會命中快取。
        std::map<std::string, std::string> reread;
        // 預先做好的 OCR（PipelineJob::prepareTicket）。用過或不能用就清掉
        struct Prepared {
            std::uint64_t ticket = 0;
            std::string language;  // PipelineJob::language：設定改了就不能沿用
            SizeI frameSize;
            OcrResult ocr;
        };
        std::optional<Prepared> prepared;
    };

    // 漫畫模式：直排的段落換成 manga-ocr 重讀的文字，ルビ 跟著搬過去（M2-03）
    void rereadMangaBlocks(const PipelineJob& job, std::vector<TextBlock>& blocks,
                           std::stop_token cancel);

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
