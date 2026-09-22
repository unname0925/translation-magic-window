#include "ui/settings_window.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <utility>

#include "ui/engine_choice.h"

namespace tmw::ui {

SettingsWindow::SettingsWindow(core::Settings settings, Encrypt encrypt, QWidget* parent)
    : QDialog(parent), settings_(std::move(settings)), encrypt_(std::move(encrypt)) {
    setWindowTitle(QStringLiteral("設定"));

    googleOnly_ = new QRadioButton(
        QStringLiteral("Google（免費、不用金鑰；用多了會被限流，翻譯品質也較差）"), this);
    useLlm_ = new QRadioButton(QStringLiteral("LLM（OpenAI 相容格式）"), this);
    // objectName 讓測試（和之後的自動化）可以直接指名抓到欄位
    googleOnly_->setObjectName(QStringLiteral("googleOnly"));
    useLlm_->setObjectName(QStringLiteral("useLlm"));

    endpoint_ = new QLineEdit(this);
    endpoint_->setObjectName(QStringLiteral("endpoint"));
    endpoint_->setPlaceholderText(QStringLiteral("http://127.0.0.1:11434/v1"));
    model_ = new QLineEdit(this);
    model_->setObjectName(QStringLiteral("model"));
    model_->setPlaceholderText(QStringLiteral("hy-mt2"));
    key_ = new QLineEdit(this);
    key_->setObjectName(QStringLiteral("key"));
    key_->setEchoMode(QLineEdit::Password);
    keyNote_ = new QLabel(this);
    keyNote_->setWordWrap(true);
    fallback_ = new QCheckBox(QStringLiteral("失敗時改用 Google"), this);
    fallback_->setObjectName(QStringLiteral("fallback"));

    auto* llmForm = new QFormLayout;
    llmForm->addRow(QStringLiteral("網址"), endpoint_);
    llmForm->addRow(QStringLiteral("模型"), model_);
    llmForm->addRow(QStringLiteral("金鑰"), key_);
    llmForm->addRow(QString(), keyNote_);
    llmForm->addRow(QString(), fallback_);

    auto* engines = new QGroupBox(QStringLiteral("翻譯引擎"), this);
    auto* engineLayout = new QVBoxLayout(engines);
    engineLayout->addWidget(googleOnly_);
    engineLayout->addWidget(useLlm_);
    engineLayout->addLayout(llmForm);

    verbose_ = new QCheckBox(
        QStringLiteral("詳細診斷（記錄檔會包含辨識到的文字和譯文，回報問題時再打開）"), this);
    verbose_->setObjectName(QStringLiteral("verbose"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("buttons"));
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("儲存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(engines);
    layout->addWidget(verbose_);
    layout->addWidget(buttons);

    connect(googleOnly_, &QRadioButton::toggled, this, [this] { updateEnabled(); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        collectFromWidgets();
        emit saved(settings_);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    applyToWidgets();
}

void SettingsWindow::applyToWidgets() {
    const EngineChoice choice = engineChoiceFrom(settings_);
    useLlm_->setChecked(choice.useLlm);
    googleOnly_->setChecked(!choice.useLlm);
    endpoint_->setText(QString::fromStdString(choice.endpoint));
    model_->setText(QString::fromStdString(choice.model));
    fallback_->setChecked(choice.fallbackToGoogle);
    verbose_->setChecked(settings_.verboseDiagnostics);
    key_->clear();
    keyNote_->setText(choice.hasKey ? QStringLiteral("已經設定過金鑰。留空表示不更改。")
                                    : QStringLiteral("本機服務（Ollama、LM Studio）不用填金鑰。"));
    updateEnabled();
}

void SettingsWindow::updateEnabled() {
    const bool llm = useLlm_->isChecked();
    for (QWidget* widget : {static_cast<QWidget*>(endpoint_), static_cast<QWidget*>(model_),
                            static_cast<QWidget*>(key_), static_cast<QWidget*>(fallback_)}) {
        widget->setEnabled(llm);
    }
}

void SettingsWindow::collectFromWidgets() {
    EngineChoice choice;
    choice.useLlm = useLlm_->isChecked();
    choice.endpoint = endpoint_->text().trimmed().toStdString();
    choice.model = model_->text().trimmed().toStdString();
    choice.fallbackToGoogle = fallback_->isChecked();

    // 空白代表「不更改」，原本的金鑰會被沿用（enginesFor 處理）
    const std::string typed = key_->text().toStdString();
    const std::string encrypted = typed.empty() || !encrypt_ ? std::string() : encrypt_(typed);

    settings_.engines = enginesFor(choice, settings_, encrypted);
    settings_.verboseDiagnostics = verbose_->isChecked();
    key_->clear();
}

}  // namespace tmw::ui
