#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "app/ollama_launcher.h"
#include "app/web_service.h"
#include "core/auto_trigger.h"
#include "core/clock.h"
#include "core/glossary.h"
#include "core/gpu_lock.h"
#include "core/history.h"
#include "core/perf_stats.h"
#include "core/pipeline.h"
#include "core/pipeline_worker.h"
#include "core/settings.h"
#include "core/web_pipeline_worker.h"
#include "net/update_check.h"
#include "ocr/ocr_service.h"
#include "platform/capture_frame_source.h"
#include "platform/debug_overlay_window.h"
#include "platform/lens_window.h"
#include "platform/overlay_renderer.h"
#include "platform/screen_capture.h"
#include "platform/translation_overlay_window.h"
#include "platform/tray_icon.h"
#include "ui/result_window.h"
#include "ui/settings_window.h"

namespace tmw::app {

// 程式的主控：一個看不見的視窗負責接收系統匣事件、計時器和其他執行個體的通知，
// 並把透鏡、螢幕擷取和自動觸發串起來。
class AppController {
public:
    // dataDirectory：程式寫出的檔案（擷取的 PNG、記錄）要放在哪裡
    // settingsPath：設定檔的位置，結果視窗的位置和字級會存回去
    // ocrDevice：OCR 要用哪個裝置（--ocr-device）。預設 Auto：先試 DirectML，失敗改用 CPU。
    AppController(HINSTANCE instance, std::filesystem::path dataDirectory,
                  std::filesystem::path settingsPath, core::Settings settings,
                  ocr::Device ocrDevice = ocr::Device::Auto);
    ~AppController();

    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    // 執行訊息迴圈，直到使用者選擇「結束」。
    int run();

    // 由第二個執行個體呼叫：通知已在執行的執行個體把透鏡顯示出來。
    static void notifyRunningInstance();

private:
    // 一個透鏡，和只屬於它的東西（M5-05：可以開好幾個透鏡）。
    // 成員的宣告順序就是解構的反序：視窗最先消失，不會再呼叫已經不在的觸發器
    struct Lens {
        int id = 1;  // 處理管線用它分開各透鏡的記憶；關掉的編號不再使用
        std::unique_ptr<core::AutoTrigger> trigger;
        std::unique_ptr<platform::LensWindow> window;
        // 最後一次處理的結果，除錯傾印、覆蓋框和譯文覆蓋層要用
        std::optional<core::PipelineResult> lastResult;
        // 譯文覆蓋層（打開「在原位顯示譯文」時才有）和目前貼著的是哪一次的結果
        std::unique_ptr<platform::TranslationOverlayWindow> overlay;
        std::uint64_t overlayGeneration = 0;
        // 除錯覆蓋框，和上一次畫的是什麼（一樣就不重畫，每 100 毫秒會檢查一次）
        std::unique_ptr<platform::DebugOverlayWindow> debugOverlay;
        core::LensState debugOverlayState = core::LensState::Showing;
        std::uint64_t debugOverlayGeneration = 0;
        core::RectI debugOverlayRect{};
        bool flashing = false;
    };

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void showTrayMenu(POINT anchor);
    // 所有透鏡一起藏起或叫出來
    void setLensVisible(bool visible);
    // 新增一個透鏡（最多 kMaxLenses 個）。placement 是上次記住的位置；
    // 沒有（或已經不在任何螢幕上）時放在前一個透鏡的右下方
    void addLens(const core::RectI* placement = nullptr);
    // 關掉最後新增的那個透鏡（第一個透鏡不能關，只能藏起來）
    void removeLens();
    Lens* findLens(int id);
    // 最後拖動過的透鏡：擷取、除錯傾印用它
    Lens& activeLens();
    // 「立即翻譯」：每個顯示中的透鏡都翻一次
    void translateNow();

    // 畫面穩定後的「處理」：擷取透鏡底下的畫面，交給處理管線。
    void process(Lens& lens, const core::ProcessRequest& request);
    // 畫面還在等穩定時先做 OCR（AutoTrigger 的 onPrepare，速度優化 4）
    void prepare(Lens& lens, std::uint64_t ticket);
    // 網頁漫畫：開管道等瀏覽器擴充功能連線（setUpPipeline 的最後）
    void setUpWeb();
    // 翻譯失敗或重建翻譯服務之後：本機 Ollama 沒在跑就啟動它
    void ensureOllama();
    // 開管道；開不起來時過一下再試（attempt：第幾次）
    void startWeb(int attempt);

    // 處理管線的結果回到 UI 執行緒之後
    void onPipelineResult(const core::PipelineResult& result);

    // 建立 OCR、翻譯服務和處理管線。模型或設定有問題時只記錄，程式照常執行（只是不會翻譯）。
    void setUpPipeline();
    void showResultWindow();
    void openSettings();
    // 把「現在發生了什麼」存成一個資料夾：擷取的畫面、OCR、譯文、設定（已移除金鑰）。
    // 回傳資料夾的位置，失敗時是空的。
    std::filesystem::path writeDebugDump();
    // 除錯覆蓋框：打開／關閉，以及「內容有變才重畫」
    void setDebugOverlayEnabled(bool enabled);
    // 漫畫模式：同一個對話框裡的行就是同一段（M2-02）。模型載入失敗時維持關閉並記錄原因。
    void setMangaMode(bool enabled);
    // 辨識語言："auto"、"ja"、"en"、"ko"。下一次處理就生效（design.md 4.4「語言判斷」）
    void setOcrLanguage(const std::string& code);
    // 遊戲模式：第一次辨識之後只看文字區域（M2-06）
    void setGameMode(bool enabled);
    // 情境模式（M2-06）：""（不使用）、"manga"、"game"、"web"
    void setProfile(const std::string& id);
    // settings_ 裡現在生效的值套用到 OCR、觸發器、處理管線
    void applyProfileValues();
    // 依 settings_.hotkeys 註冊全域快捷鍵（M2-10）。回傳註冊不了的（被其他程式佔用或看不懂），
    // 例如「Ctrl+Alt+T（立即翻譯）」
    std::vector<std::string> registerHotkeys();
    void unregisterHotkeys();
    void refreshDebugOverlay(Lens& lens);
    // glossary.txt 改過（或第一次）就重新讀取，回傳目前的詞表；沒有檔案時是 nullptr
    std::shared_ptr<const core::Glossary> currentGlossary();
    // 用預設的編輯器打開 glossary.txt，還沒有的話先建立一份附說明的
    void openGlossary();
    // 設定改了之後：存檔、換掉翻譯引擎鏈、更新記錄的詳細程度
    void applySettings(const core::Settings& settings);
    // 重新建立翻譯服務和處理管線（OCR 不用重建）
    void rebuildTranslation();
    void setPaused(bool paused);
    void saveSettings();

    // 把透鏡範圍擷取下來存成 PNG（開發用的驗證工具）
    bool saveLensCapture(Lens& lens);

    // 邊框顏色：平常依照觸發狀態，手動擷取時短暫閃一下成功或失敗的顏色
    void updateAccent(Lens& lens);
    // 在原位顯示譯文（M3）。只在「顯示結果中」而且透鏡沒動過時顯示；
    // 畫面一變、開始拖動或重新處理就先藏起來（M3-04），不會留下錯位的譯文
    void setOverlayEnabled(bool enabled);
    // 套用設定裡的譯文字型（M4-04）。字型檔不在時用微軟正黑體
    void applyOverlayFont();
    // 背景修補（M4-01）：OCR 用顯示卡而且有 LaMa 模型時才有
    void setUpInpainter();
    void refreshOverlay(Lens& lens);
    // 建立這個透鏡的譯文覆蓋層和除錯覆蓋框（有打開的話）
    void attachOverlays(Lens& lens);
    void flashLens(Lens& lens, core::Rgba accent);
    // 檢查新版本（M5-04）：啟動 2 分鐘後第一次，之後每 24 小時一次，在背景執行緒連 GitHub
    void maybeCheckForUpdates();
    void onUpdateChecked(std::optional<net::ReleaseInfo> release);

    std::filesystem::path dataDirectory_;
    HWND hwnd_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    UINT showLensMessage_ = 0;
    bool captureHotkeyRegistered_ = false;
    bool translateHotkeyRegistered_ = false;
    bool debugDumpHotkeyRegistered_ = false;
    bool autoSave_ = false;
    bool paused_ = false;
    std::filesystem::path settingsPath_;
    core::Settings settings_;
    // 專有名詞表（M2-09）：資料資料夾裡的 glossary.txt，改檔後的下一次處理就重新讀取
    std::shared_ptr<const core::Glossary> glossary_;
    // ルビ的一般讀音表（M2-13）。沒有時是 nullptr
    std::shared_ptr<const core::FuriganaReadings> furigana_;
    std::filesystem::file_time_type glossaryTime_{};
    ocr::Device ocrDevice_ = ocr::Device::Auto;

    // 宣告順序就是建構順序；解構時反過來，透鏡最先消失，不會再觸發回呼
    core::SteadyClock clock_;
    std::unique_ptr<platform::ScreenCapture> capture_;
    std::unique_ptr<platform::CaptureFrameSource> frameSource_;
    // 翻譯：OCR 和引擎鏈建立失敗時這些會是空的，程式照常執行
    std::unique_ptr<ocr::OcrService> ocr_;
    std::shared_ptr<core::TranslationService> translation_;
    // 設定的引擎是本機 Ollama、但它沒在跑時把它叫起來（開機後不一定會自己啟動）
    OllamaLauncher ollama_;
    // 顯示卡上的推論（OCR、背景修補）輪流做：透鏡和網頁漫畫在不同的執行緒（core/gpu_lock.h）
    std::mutex gpu_;
    std::unique_ptr<core::LockedOcrService> lockedOcr_;
    std::unique_ptr<core::Pipeline> pipeline_;
    std::unique_ptr<core::PipelineWorker> worker_;
    // 網頁漫畫整頁翻譯：自己的處理管線和工作佇列，瀏覽器擴充功能的連線。
    // 排在後面，解構時先停：先關連線，再停工作佇列
    std::unique_ptr<core::Pipeline> webPipeline_;
    std::unique_ptr<core::WebPipelineWorker> webWorker_;
    std::unique_ptr<WebService> web_;
    core::History history_;
    std::unique_ptr<ui::ResultWindow> resultWindow_;
    std::unique_ptr<ui::SettingsWindow> settingsWindow_;
    // 每個步驟的耗時，除錯傾印會附上統計（M1-15）
    core::PerfStats perf_;
    bool debugOverlayEnabled_ = false;
    // 譯文覆蓋層的繪製器（所有透鏡共用）。關掉時是空的
    std::unique_ptr<platform::OverlayRenderer> overlayRenderer_;
    // 背景修補。工作執行緒在用的時候 job 也握著它，所以是 shared_ptr
    std::shared_ptr<core::IInpainter> inpainter_;

    std::unique_ptr<platform::TrayIcon> tray_;
    // 透鏡（M5-05）。第一個一定在；放在最後，解構時最先消失
    std::vector<std::unique_ptr<Lens>> lenses_;
    int nextLensId_ = 1;
    int activeLensId_ = 1;

    // 檢查新版本（M5-04）。有比現在新的版本時，系統匣選單最上面出現「下載新版本」
    std::optional<net::ReleaseInfo> availableUpdate_;
    std::chrono::steady_clock::time_point startedAt_ = std::chrono::steady_clock::now();
    std::jthread updateCheck_;  // 解構時請它停下來並等它結束

public:
    // 同時最多幾個透鏡。每個透鏡處理時都要 OCR，太多會互相等待
    static constexpr std::size_t kMaxLenses = 4;
};

}  // namespace tmw::app
