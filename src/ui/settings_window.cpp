#include "ui/settings_window.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

#include "core/custom_http_check.h"
#include "core/hotkey.h"
#include "core/overlay_font.h"
#include "ui/engine_choice.h"

namespace tmw::ui {

SettingsWindow::SettingsWindow(core::Settings settings, Encrypt encrypt, QWidget* parent)
    : QDialog(parent), settings_(std::move(settings)), encrypt_(std::move(encrypt)) {
    setWindowTitle(QStringLiteral("設定"));

    googleOnly_ = new QRadioButton(
        QStringLiteral("Google（免費、不用金鑰；用多了會被限流，翻譯品質也較差）"), this);
    useLlm_ = new QRadioButton(QStringLiteral("其他引擎（LLM 或付費翻譯服務）"), this);
    // objectName 讓測試（和之後的自動化）可以直接指名抓到欄位
    googleOnly_->setObjectName(QStringLiteral("googleOnly"));
    useLlm_->setObjectName(QStringLiteral("useLlm"));

    // LLM 的格式（M2-07）：同一份 OpenAI 相容的實作接得到大部分服務，Claude 用它的原生 API
    llmKind_ = new QComboBox(this);
    llmKind_->setObjectName(QStringLiteral("llmKind"));
    llmKind_->addItem(QStringLiteral("OpenAI 相容（Ollama、LM Studio、OpenAI、Gemini…）"),
                      QStringLiteral("openai-compatible"));
    llmKind_->addItem(QStringLiteral("Claude（Anthropic 官方 API）"), QStringLiteral("anthropic"));
    llmKind_->addItem(QStringLiteral("DeepL"), QStringLiteral("deepl"));
    llmKind_->addItem(QStringLiteral("Microsoft Translator"), QStringLiteral("azure"));
    llmKind_->addItem(QStringLiteral("Google Cloud Translation"), QStringLiteral("google-cloud"));
    llmKind_->addItem(QStringLiteral("自訂 HTTP 範本"), QStringLiteral("custom-http"));
    // 自訂 HTTP 範本的欄位（net/custom_http_translator.h）
    headers_ = new QPlainTextEdit(this);
    headers_->setObjectName(QStringLiteral("headers"));
    headers_->setPlaceholderText(QStringLiteral("一行一個，例如\nAuthorization: Bearer {{key}}"));
    headers_->setMaximumHeight(70);
    bodyTemplate_ = new QPlainTextEdit(this);
    bodyTemplate_->setObjectName(QStringLiteral("bodyTemplate"));
    bodyTemplate_->setPlaceholderText(QStringLiteral(
        "{\"q\": \"{{text}}\", \"source\": \"{{source}}\", \"target\": \"{{target}}\"}\n"
        "（空的就用 GET，原文放在網址的 {{text}}）"));
    bodyTemplate_->setMaximumHeight(70);
    responsePath_ = new QLineEdit(this);
    responsePath_->setObjectName(QStringLiteral("responsePath"));
    responsePath_->setPlaceholderText(QStringLiteral("譯文在回應裡的位置，例如 translatedText"));
    region_ = new QLineEdit(this);
    region_->setObjectName(QStringLiteral("region"));
    region_->setPlaceholderText(QStringLiteral("全域資源留空；區域型資源填區域，例如 eastasia"));
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
    llmForm->addRow(QStringLiteral("引擎"), llmKind_);
    llmForm->addRow(QStringLiteral("網址"), endpoint_);
    llmForm->addRow(QStringLiteral("模型"), model_);
    llmForm->addRow(QStringLiteral("區域"), region_);
    llmForm->addRow(QStringLiteral("標頭"), headers_);
    llmForm->addRow(QStringLiteral("請求內容"), bodyTemplate_);
    llmForm->addRow(QStringLiteral("譯文路徑"), responsePath_);
    engineProblem_ = new QLabel(this);
    engineProblem_->setObjectName(QStringLiteral("engineProblem"));
    engineProblem_->setWordWrap(true);
    engineProblem_->setStyleSheet(QStringLiteral("color: #c62828;"));
    engineProblem_->hide();
    llmForm->addRow(engineProblem_);
    llmForm_ = llmForm;
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
    checkUpdates_ = new QCheckBox(
        QStringLiteral("每天檢查一次有沒有新版本（連到 GitHub，只讀取最新版本的編號）"), this);
    checkUpdates_->setObjectName(QStringLiteral("checkUpdates"));
    mangaMode_ = new QCheckBox(
        QStringLiteral(
            "漫畫模式（同一個對話框裡的字當成同一句；每次多約 40 ms，網頁和遊戲不用開）"),
        this);
    mangaMode_->setObjectName(QStringLiteral("mangaMode"));
    gameMode_ = new QCheckBox(
        QStringLiteral("遊戲模式（只看文字區域有沒有變；閃爍的游標、角色動畫不會讓翻譯一直等）"),
        this);
    gameMode_->setObjectName(QStringLiteral("gameMode"));
    // 情境模式（M2-06）：下面這幾項記在目前的情境裡，切到別的情境時各自恢復
    profileNote_ = new QLabel(this);
    profileNote_->setObjectName(QStringLiteral("profileNote"));
    profileNote_->setWordWrap(true);
    translateEdges_ = new QCheckBox(
        QStringLiteral(
            "也翻譯碰到透鏡邊緣的句子（捲動網頁時會翻到殘句；遊戲對話框常貼著邊時要打開）"),
        this);
    translateEdges_->setObjectName(QStringLiteral("translateEdges"));
    settleMs_ = new QSpinBox(this);
    settleMs_->setObjectName(QStringLiteral("settleMs"));
    settleMs_->setRange(100, 3000);
    settleMs_->setSingleStep(50);
    settleMs_->setSuffix(QStringLiteral(" 毫秒"));

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
    languageForm->addRow(QStringLiteral("畫面停下來多久才翻"), settleMs_);
    // 在原位顯示譯文時用的字型（M4-04）。內建字型要先用 tools/fetch_models 下載
    overlayFont_ = new QComboBox(this);
    overlayFont_->setObjectName(QStringLiteral("overlayFont"));
    for (const core::OverlayFont& font : core::kOverlayFonts) {
        overlayFont_->addItem(QString::fromUtf8(font.label.data(), font.label.size()),
                              QString::fromUtf8(font.id.data(), font.id.size()));
    }
    languageForm->addRow(QStringLiteral("譯文字型"), overlayFont_);

    // 快捷鍵（M2-10）：點一下欄位直接按想要的組合；清掉代表不使用那個快捷鍵
    const auto hotkeyEdit = [this](const char* name) {
        auto* edit = new QKeySequenceEdit(this);
        edit->setObjectName(QString::fromLatin1(name));
        edit->setMaximumSequenceLength(1);
        edit->setClearButtonEnabled(true);
        return edit;
    };
    hotkeyTranslate_ = hotkeyEdit("hotkeyTranslate");
    hotkeyDebugDump_ = hotkeyEdit("hotkeyDebugDump");
    hotkeyCapture_ = hotkeyEdit("hotkeyCapture");
    auto* hotkeys = new QGroupBox(QStringLiteral("快捷鍵"), this);
    auto* hotkeyForm = new QFormLayout(hotkeys);
    hotkeyForm->addRow(QStringLiteral("立即翻譯"), hotkeyTranslate_);
    hotkeyForm->addRow(QStringLiteral("除錯傾印"), hotkeyDebugDump_);
    hotkeyForm->addRow(QStringLiteral("擷取透鏡範圍"), hotkeyCapture_);
    hotkeyProblem_ = new QLabel(this);
    hotkeyProblem_->setObjectName(QStringLiteral("hotkeyProblem"));
    hotkeyProblem_->setWordWrap(true);
    hotkeyProblem_->setStyleSheet(QStringLiteral("color: #c62828;"));
    hotkeyProblem_->hide();
    hotkeyForm->addRow(hotkeyProblem_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("buttons"));
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("儲存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(engines);
    layout->addWidget(profileNote_);
    layout->addLayout(languageForm);
    layout->addWidget(mangaMode_);
    layout->addWidget(gameMode_);
    layout->addWidget(translateEdges_);
    layout->addWidget(verbose_);
    layout->addWidget(checkUpdates_);
    layout->addWidget(hotkeys);
    layout->addWidget(buttons);

    connect(googleOnly_, &QRadioButton::toggled, this, [this] { updateEnabled(); });
    connect(llmKind_, &QComboBox::currentIndexChanged, this, [this] { updateLlmHints(); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        // 快捷鍵有問題（看不懂、兩組一樣）就不存，說明原因後讓使用者改
        const std::vector<std::string> names{"立即翻譯", "除錯傾印", "擷取透鏡範圍"};
        const std::vector<std::string> texts{
            hotkeyText(hotkeyTranslate_), hotkeyText(hotkeyDebugDump_), hotkeyText(hotkeyCapture_)};
        if (const std::string problem = core::hotkeyProblem(names, texts); !problem.empty()) {
            hotkeyProblem_->setText(QString::fromStdString(problem));
            hotkeyProblem_->show();
            return;
        }
        hotkeyProblem_->hide();
        // 自訂 HTTP 範本有問題就不存（存了也不能用，引擎鏈會略過它）
        if (useLlm_->isChecked() &&
            llmKind_->currentData().toString() == QStringLiteral("custom-http")) {
            const std::string problem = core::customHttpProblem(
                endpoint_->text().trimmed().toStdString(), headers_->toPlainText().toStdString(),
                bodyTemplate_->toPlainText().toStdString(),
                responsePath_->text().trimmed().toStdString());
            if (!problem.empty()) {
                engineProblem_->setText(QString::fromStdString(problem));
                engineProblem_->show();
                return;
            }
        }
        engineProblem_->hide();
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
    region_->setText(QString::fromStdString(choice.region));
    headers_->setPlainText(QString::fromStdString(choice.headers));
    bodyTemplate_->setPlainText(QString::fromStdString(choice.bodyTemplate));
    responsePath_->setText(QString::fromStdString(choice.responsePath));
    const int kind = llmKind_->findData(QString::fromStdString(choice.engineId));
    llmKind_->setCurrentIndex(kind < 0 ? 0 : kind);
    fallback_->setChecked(choice.fallbackToGoogle);
    verbose_->setChecked(settings_.verboseDiagnostics);
    checkUpdates_->setChecked(settings_.checkUpdates);
    mangaMode_->setChecked(settings_.mangaMode);
    gameMode_->setChecked(settings_.gameMode);
    translateEdges_->setChecked(!settings_.dropEdgeBlocks);
    settleMs_->setValue(settings_.settleMs);
    profileNote_->setText(
        settings_.profile.empty()
            ? QStringLiteral(
                  "情境：不使用（可以在系統匣的「情境」選漫畫、遊戲或網頁，各自記住一組設定）")
            : QString::fromStdString("情境：" + core::profileName(settings_.profile) +
                                     "（下面的辨識設定會記在這個情境裡）"));
    const int language = ocrLanguage_->findData(QString::fromStdString(settings_.ocrLanguage));
    ocrLanguage_->setCurrentIndex(language < 0 ? 0 : language);
    const int font = overlayFont_->findData(QString::fromStdString(settings_.overlayFont));
    overlayFont_->setCurrentIndex(font < 0 ? 0 : font);
    key_->clear();
    showHotkey(hotkeyTranslate_, settings_.hotkeys.translate);
    showHotkey(hotkeyDebugDump_, settings_.hotkeys.debugDump);
    showHotkey(hotkeyCapture_, settings_.hotkeys.capture);
    updateLlmHints();
    updateEnabled();
}

std::string SettingsWindow::hotkeyText(const QKeySequenceEdit* edit) {
    const QKeySequence sequence = edit->keySequence();
    if (sequence.isEmpty()) {
        return {};
    }
    // Qt 的寫法（「Ctrl+Alt+Meta+T」）轉成設定檔的寫法；看不懂的原樣留著，存檔前的檢查會擋下來
    const std::string text = sequence.toString(QKeySequence::PortableText).toStdString();
    const std::optional<core::Hotkey> hotkey = core::parseHotkey(text);
    return hotkey ? core::formatHotkey(*hotkey) : text;
}

void SettingsWindow::showHotkey(QKeySequenceEdit* edit, const std::string& text) {
    // 設定檔的 Win 在 Qt 叫 Meta
    std::string qt = text;
    if (const std::size_t at = qt.find("Win"); at != std::string::npos) {
        qt.replace(at, 3, "Meta");
    }
    edit->setKeySequence(
        QKeySequence::fromString(QString::fromStdString(qt), QKeySequence::PortableText));
}

void SettingsWindow::updateLlmHints() {
    const std::string kind = llmKind_->currentData().toString().toStdString();
    // 網址留空就用各家的預設值；OpenAI 相容格式預設接本機的 Ollama
    const QString endpoint =
        kind == "anthropic"      ? QStringLiteral("https://api.anthropic.com")
        : kind == "deepl"        ? QStringLiteral("依金鑰自動選免費版或付費版")
        : kind == "azure"        ? QStringLiteral("https://api.cognitive.microsofttranslator.com")
        : kind == "google-cloud" ? QStringLiteral("https://translation.googleapis.com")
                                 : QStringLiteral("http://127.0.0.1:11434/v1");
    endpoint_->setPlaceholderText(endpoint);
    model_->setPlaceholderText(kind == "anthropic" ? QStringLiteral("claude-opus-5")
                                                   : QStringLiteral("hy-mt2"));
    // 模型只有 LLM 要填，區域只有 Microsoft 要填
    llmForm_->setRowVisible(model_, isLlmEngine(kind));
    llmForm_->setRowVisible(region_, kind == "azure");
    const bool custom = kind == "custom-http";
    llmForm_->setRowVisible(headers_, custom);
    llmForm_->setRowVisible(bodyTemplate_, custom);
    llmForm_->setRowVisible(responsePath_, custom);
    if (custom) {
        endpoint_->setPlaceholderText(
            QStringLiteral("https://…（可以用 {{text}} {{source}} {{key}}）"));
    }
    // 金鑰是分開存的：換了引擎，原本那一家的金鑰不會拿來用
    const bool hasKey = std::ranges::any_of(settings_.engines, [&](const core::EngineSettings& e) {
        return e.id == kind && !e.encryptedApiKey.empty();
    });
    const QString needKey =
        kind == "anthropic" ? QStringLiteral("Claude 一定要金鑰（在 console.anthropic.com 申請）。")
        : kind == "deepl"   ? QStringLiteral("DeepL 一定要金鑰（免費方案每月 50 萬字）。")
        : kind == "azure" ? QStringLiteral("Microsoft Translator 一定要金鑰（Azure 的翻譯資源）。")
        : kind == "google-cloud"
            ? QStringLiteral("Google Cloud 一定要 API 金鑰（要先啟用 Cloud Translation API）。")
            : QStringLiteral("本機服務（Ollama、LM Studio）不用填金鑰。");
    QString note = hasKey ? QStringLiteral("已經設定過金鑰。留空表示不更改。") : needKey;
    if (!isLlmEngine(kind)) {
        note += QStringLiteral("翻譯服務不會照專有名詞表翻。");
    }
    keyNote_->setText(note);
}

void SettingsWindow::updateEnabled() {
    const bool llm = useLlm_->isChecked();
    for (QWidget* widget : {static_cast<QWidget*>(llmKind_), static_cast<QWidget*>(endpoint_),
                            static_cast<QWidget*>(model_), static_cast<QWidget*>(region_),
                            static_cast<QWidget*>(headers_), static_cast<QWidget*>(bodyTemplate_),
                            static_cast<QWidget*>(responsePath_), static_cast<QWidget*>(key_),
                            static_cast<QWidget*>(fallback_)}) {
        widget->setEnabled(llm);
    }
}

void SettingsWindow::collectFromWidgets() {
    EngineChoice choice;
    choice.useLlm = useLlm_->isChecked();
    choice.engineId = llmKind_->currentData().toString().toStdString();
    choice.endpoint = endpoint_->text().trimmed().toStdString();
    choice.model = model_->text().trimmed().toStdString();
    choice.region = region_->text().trimmed().toStdString();
    choice.headers = headers_->toPlainText().toStdString();
    choice.bodyTemplate = bodyTemplate_->toPlainText().toStdString();
    choice.responsePath = responsePath_->text().trimmed().toStdString();
    choice.fallbackToGoogle = fallback_->isChecked();

    // 空白代表「不更改」，原本的金鑰會被沿用（enginesFor 處理）
    const std::string typed = key_->text().toStdString();
    const std::string encrypted = typed.empty() || !encrypt_ ? std::string() : encrypt_(typed);

    settings_.engines = enginesFor(choice, settings_, encrypted);
    settings_.verboseDiagnostics = verbose_->isChecked();
    settings_.checkUpdates = checkUpdates_->isChecked();
    settings_.mangaMode = mangaMode_->isChecked();
    settings_.gameMode = gameMode_->isChecked();
    settings_.dropEdgeBlocks = !translateEdges_->isChecked();
    settings_.settleMs = settleMs_->value();
    settings_.ocrLanguage = ocrLanguage_->currentData().toString().toStdString();
    settings_.overlayFont = overlayFont_->currentData().toString().toStdString();
    settings_.hotkeys.translate = hotkeyText(hotkeyTranslate_);
    settings_.hotkeys.debugDump = hotkeyText(hotkeyDebugDump_);
    settings_.hotkeys.capture = hotkeyText(hotkeyCapture_);
    key_->clear();
}

}  // namespace tmw::ui
