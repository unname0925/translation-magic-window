#include "ui/settings_window.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

#include "ui/engine_choice.h"

namespace tmw::ui {

SettingsWindow::SettingsWindow(core::Settings settings, Encrypt encrypt, QWidget* parent)
    : QDialog(parent), settings_(std::move(settings)), encrypt_(std::move(encrypt)) {
    setWindowTitle(QStringLiteral("設定"));

    googleOnly_ = new QRadioButton(
        QStringLiteral("Google（免費、不用金鑰；用多了會被限流，翻譯品質也較差）"), this);
    useLlm_ = new QRadioButton(QStringLiteral("LLM"), this);
    // objectName 讓測試（和之後的自動化）可以直接指名抓到欄位
    googleOnly_->setObjectName(QStringLiteral("googleOnly"));
    useLlm_->setObjectName(QStringLiteral("useLlm"));

    // LLM 的格式（M2-07）：同一份 OpenAI 相容的實作接得到大部分服務，Claude 用它的原生 API
    llmKind_ = new QComboBox(this);
    llmKind_->setObjectName(QStringLiteral("llmKind"));
    llmKind_->addItem(QStringLiteral("OpenAI 相容（Ollama、LM Studio、OpenAI、Gemini…）"),
                      QStringLiteral("openai-compatible"));
    llmKind_->addItem(QStringLiteral("Claude（Anthropic 官方 API）"), QStringLiteral("anthropic"));
    endpoint_ = new QLineEdit(this);
    endpoint_->setObjectName(QStringLiteral("endpoint"));
    model_ = new QLineEdit(this);
    model_->setObjectName(QStringLiteral("model"));
    key_ = new QLineEdit(this);
    key_->setObjectName(QStringLiteral("key"));
    key_->setEchoMode(QLineEdit::Password);
    keyNote_ = new QLabel(this);
    keyNote_->setWordWrap(true);
    fallback_ = new QCheckBox(QStringLiteral("失敗時改用 Google"), this);
    fallback_->setObjectName(QStringLiteral("fallback"));

    auto* llmForm = new QFormLayout;
    llmForm->addRow(QStringLiteral("格式"), llmKind_);
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
    mangaMode_ = new QCheckBox(
        QStringLiteral(
            "漫畫模式（同一個對話框裡的字當成同一句；每次多約 40 ms，網頁和遊戲不用開）"),
        this);
    mangaMode_->setObjectName(QStringLiteral("mangaMode"));
    gameMode_ = new QCheckBox(
        QStringLiteral("遊戲模式（只看文字區域有沒有變；閃爍的游標、角色動畫不會讓翻譯一直等）"),
        this);
    gameMode_->setObjectName(QStringLiteral("gameMode"));

    // 自動判斷時，日文、英文的畫面只跑主模型；韓文要多跑一次判斷。
    // 固定只看某一種語言的人指定它，就連判斷都省了（design.md 4.4「語言判斷」）。
    ocrLanguage_ = new QComboBox(this);
    ocrLanguage_->setObjectName(QStringLiteral("ocrLanguage"));
    ocrLanguage_->addItem(QStringLiteral("自動判斷"), QStringLiteral("auto"));
    ocrLanguage_->addItem(QStringLiteral("日文"), QStringLiteral("ja"));
    ocrLanguage_->addItem(QStringLiteral("英文"), QStringLiteral("en"));
    ocrLanguage_->addItem(QStringLiteral("韓文"), QStringLiteral("ko"));
    auto* languageForm = new QFormLayout;
    languageForm->addRow(QStringLiteral("辨識語言"), ocrLanguage_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("buttons"));
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("儲存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(engines);
    layout->addLayout(languageForm);
    layout->addWidget(mangaMode_);
    layout->addWidget(gameMode_);
    layout->addWidget(verbose_);
    layout->addWidget(buttons);

    connect(googleOnly_, &QRadioButton::toggled, this, [this] { updateEnabled(); });
    connect(llmKind_, &QComboBox::currentIndexChanged, this, [this] { updateLlmHints(); });
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
    const int kind = llmKind_->findData(QString::fromStdString(choice.llmId));
    llmKind_->setCurrentIndex(kind < 0 ? 0 : kind);
    fallback_->setChecked(choice.fallbackToGoogle);
    verbose_->setChecked(settings_.verboseDiagnostics);
    mangaMode_->setChecked(settings_.mangaMode);
    gameMode_->setChecked(settings_.gameMode);
    const int language = ocrLanguage_->findData(QString::fromStdString(settings_.ocrLanguage));
    ocrLanguage_->setCurrentIndex(language < 0 ? 0 : language);
    key_->clear();
    updateLlmHints();
    updateEnabled();
}

void SettingsWindow::updateLlmHints() {
    const std::string kind = llmKind_->currentData().toString().toStdString();
    const bool claude = kind == "anthropic";
    // 網址留空就用預設值：Claude 是官方的網址，OpenAI 相容格式預設接本機的 Ollama
    endpoint_->setPlaceholderText(claude ? QStringLiteral("https://api.anthropic.com")
                                         : QStringLiteral("http://127.0.0.1:11434/v1"));
    model_->setPlaceholderText(claude ? QStringLiteral("claude-opus-5") : QStringLiteral("hy-mt2"));
    // 金鑰是分開存的：換了格式，原本那一家的金鑰不會拿來用
    const bool hasKey = std::ranges::any_of(settings_.engines, [&](const core::EngineSettings& e) {
        return e.id == kind && !e.encryptedApiKey.empty();
    });
    keyNote_->setText(hasKey ? QStringLiteral("已經設定過金鑰。留空表示不更改。")
                      : claude
                          ? QStringLiteral("Claude 一定要金鑰（在 console.anthropic.com 申請）。")
                          : QStringLiteral("本機服務（Ollama、LM Studio）不用填金鑰。"));
}

void SettingsWindow::updateEnabled() {
    const bool llm = useLlm_->isChecked();
    for (QWidget* widget : {static_cast<QWidget*>(llmKind_), static_cast<QWidget*>(endpoint_),
                            static_cast<QWidget*>(model_), static_cast<QWidget*>(key_),
                            static_cast<QWidget*>(fallback_)}) {
        widget->setEnabled(llm);
    }
}

void SettingsWindow::collectFromWidgets() {
    EngineChoice choice;
    choice.useLlm = useLlm_->isChecked();
    choice.llmId = llmKind_->currentData().toString().toStdString();
    choice.endpoint = endpoint_->text().trimmed().toStdString();
    choice.model = model_->text().trimmed().toStdString();
    choice.fallbackToGoogle = fallback_->isChecked();

    // 空白代表「不更改」，原本的金鑰會被沿用（enginesFor 處理）
    const std::string typed = key_->text().toStdString();
    const std::string encrypted = typed.empty() || !encrypt_ ? std::string() : encrypt_(typed);

    settings_.engines = enginesFor(choice, settings_, encrypted);
    settings_.verboseDiagnostics = verbose_->isChecked();
    settings_.mangaMode = mangaMode_->isChecked();
    settings_.gameMode = gameMode_->isChecked();
    settings_.ocrLanguage = ocrLanguage_->currentData().toString().toStdString();
    key_->clear();
}

}  // namespace tmw::ui
