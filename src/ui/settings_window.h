// 簡易設定視窗（見 docs/execution-plan.md M1-13）。
//
// 只放使用者真的需要填的東西：用哪個翻譯引擎、LLM 的網址和模型、金鑰、詳細診斷。
// 金鑰用 QLineEdit::Password 輸入，存進設定檔前會先加密（platform/secret）。
// 已經存過的金鑰不會被讀出來顯示，留空就代表「不更改」。
#pragma once

#include <QDialog>
#include <functional>
#include <string>

#include "core/settings.h"

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;

namespace tmw::ui {

class SettingsWindow : public QDialog {
    Q_OBJECT

public:
    // encrypt：把使用者填的金鑰加密（正式程式用 platform::encryptSecret，測試時換成假的）
    using Encrypt = std::function<std::string(const std::string&)>;

    SettingsWindow(core::Settings settings, Encrypt encrypt, QWidget* parent = nullptr);

    // 按下「儲存」之後的設定
    const core::Settings& settings() const { return settings_; }

signals:
    void saved(const core::Settings& settings);

private:
    void applyToWidgets();
    void collectFromWidgets();
    void updateEnabled();
    // LLM 的格式換了：網址和模型的提示、金鑰說明跟著換
    void updateLlmHints();
    // 快捷鍵欄位和設定檔寫法（core/hotkey.h）之間的轉換。空欄位是空字串（不使用）。
    static std::string hotkeyText(const QKeySequenceEdit* edit);
    static void showHotkey(QKeySequenceEdit* edit, const std::string& text);

    core::Settings settings_;
    Encrypt encrypt_;

    QRadioButton* googleOnly_ = nullptr;
    QRadioButton* useLlm_ = nullptr;
    QComboBox* llmKind_ = nullptr;  // 每一項的 data 是引擎的 id
    QLineEdit* endpoint_ = nullptr;
    QLineEdit* model_ = nullptr;
    QLineEdit* key_ = nullptr;
    QLabel* keyNote_ = nullptr;
    QCheckBox* fallback_ = nullptr;
    QCheckBox* verbose_ = nullptr;
    QCheckBox* mangaMode_ = nullptr;
    QCheckBox* gameMode_ = nullptr;
    QCheckBox* translateEdges_ = nullptr;  // 勾起來代表 dropEdgeBlocks = false
    QSpinBox* settleMs_ = nullptr;
    QLabel* profileNote_ = nullptr;  // 目前的情境
    QKeySequenceEdit* hotkeyTranslate_ = nullptr;
    QKeySequenceEdit* hotkeyDebugDump_ = nullptr;
    QKeySequenceEdit* hotkeyCapture_ = nullptr;
    QLabel* hotkeyProblem_ = nullptr;   // 快捷鍵有問題時的說明（紅字），存檔時才檢查
    QComboBox* ocrLanguage_ = nullptr;  // 每一項的 data 是設定檔裡的值（"auto"、"ja"…）
};

}  // namespace tmw::ui
