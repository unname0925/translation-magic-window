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
class QLabel;
class QLineEdit;
class QRadioButton;

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

    core::Settings settings_;
    Encrypt encrypt_;

    QRadioButton* googleOnly_ = nullptr;
    QRadioButton* useLlm_ = nullptr;
    QLineEdit* endpoint_ = nullptr;
    QLineEdit* model_ = nullptr;
    QLineEdit* key_ = nullptr;
    QLabel* keyNote_ = nullptr;
    QCheckBox* fallback_ = nullptr;
    QCheckBox* verbose_ = nullptr;
};

}  // namespace tmw::ui
