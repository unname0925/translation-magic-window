#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

#include "core/auto_trigger.h"
#include "core/clock.h"
#include "core/glossary.h"
#include "core/history.h"
#include "core/perf_stats.h"
#include "core/pipeline.h"
#include "core/pipeline_worker.h"
#include "core/settings.h"
#include "ocr/ocr_service.h"
#include "platform/capture_frame_source.h"
#include "platform/debug_overlay_window.h"
#include "platform/lens_window.h"
#include "platform/screen_capture.h"
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
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void showTrayMenu(POINT anchor);
    void setLensVisible(bool visible);

    // 畫面穩定後的「處理」：擷取透鏡底下的畫面，交給處理管線。
    void process(const core::ProcessRequest& request);

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
    // 依 settings_.hotkeys 註冊全域快捷鍵（M2-10）。回傳註冊不了的（被其他程式佔用或看不懂），
    // 例如「Ctrl+Alt+T（立即翻譯）」
    std::vector<std::string> registerHotkeys();
    void unregisterHotkeys();
    void refreshDebugOverlay();
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
    bool saveLensCapture();

    // 邊框顏色：平常依照觸發狀態，手動擷取時短暫閃一下成功或失敗的顏色
    void updateAccent();
    void flashLens(core::Rgba accent);

    std::filesystem::path dataDirectory_;
    HWND hwnd_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    UINT showLensMessage_ = 0;
    bool captureHotkeyRegistered_ = false;
    bool translateHotkeyRegistered_ = false;
    bool debugDumpHotkeyRegistered_ = false;
    bool autoSave_ = false;
    bool flashing_ = false;
    bool paused_ = false;
    std::filesystem::path settingsPath_;
    core::Settings settings_;
    // 專有名詞表（M2-09）：資料資料夾裡的 glossary.txt，改檔後的下一次處理就重新讀取
    std::shared_ptr<const core::Glossary> glossary_;
    std::filesystem::file_time_type glossaryTime_{};
    ocr::Device ocrDevice_ = ocr::Device::Auto;

    // 宣告順序就是建構順序；解構時反過來，透鏡最先消失，不會再觸發回呼
    core::SteadyClock clock_;
    std::unique_ptr<platform::ScreenCapture> capture_;
    std::unique_ptr<platform::CaptureFrameSource> frameSource_;
    std::unique_ptr<core::AutoTrigger> trigger_;
    // 翻譯：OCR 和引擎鏈建立失敗時這些會是空的，程式照常執行
    std::unique_ptr<ocr::OcrService> ocr_;
    std::shared_ptr<core::TranslationService> translation_;
    std::unique_ptr<core::Pipeline> pipeline_;
    std::unique_ptr<core::PipelineWorker> worker_;
    core::History history_;
    std::unique_ptr<ui::ResultWindow> resultWindow_;
    std::unique_ptr<ui::SettingsWindow> settingsWindow_;
    // 最後一次處理的結果，除錯傾印和覆蓋框要用
    std::optional<core::PipelineResult> lastResult_;
    // 每個步驟的耗時，除錯傾印會附上統計（M1-15）
    core::PerfStats perf_;
    std::unique_ptr<platform::DebugOverlayWindow> debugOverlay_;
    // 上一次畫的是什麼，一樣就不重畫（每 100 毫秒會檢查一次）
    core::LensState debugOverlayState_ = core::LensState::Showing;
    std::uint64_t debugOverlayGeneration_ = 0;
    core::RectI debugOverlayRect_{};

    std::unique_ptr<platform::TrayIcon> tray_;
    std::unique_ptr<platform::LensWindow> lens_;
};

}  // namespace tmw::app
