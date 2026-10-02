// 設定視窗（M1-13）。驗收條件是「設定儲存後重新啟動，內容還在，且金鑰是加密的」，
// 所以這裡走完整條路：在畫面上填東西 → 存檔 → 重新讀檔 → 檢查內容和金鑰。
#include "ui/settings_window.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>

#include "core/hotkey.h"
#include "core/settings.h"
#include "platform/secret.h"
#include "platform/settings_file.h"

namespace tmw::ui {
namespace {

class SettingsWindowTest : public ::testing::Test {
protected:
    void SetUp() override {
        directory_ = std::filesystem::temp_directory_path() /
                     ("tmw-settings-" + std::to_string(GetCurrentProcessId()) + "-" +
                      ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(directory_);
        std::filesystem::create_directories(directory_);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    std::filesystem::path path() const { return platform::settingsPathIn(directory_); }

    // 按下「儲存」，並把設定寫進檔案（正式程式的 AppController::applySettings 也是這樣做）
    core::Settings save(SettingsWindow& window) {
        core::Settings saved;
        QObject::connect(&window, &SettingsWindow::saved,
                         [&saved](const core::Settings& settings) { saved = settings; });
        window.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        platform::saveSettings(path(), saved);
        return saved;
    }

    std::filesystem::path directory_;
};

QLineEdit* field(SettingsWindow& window, const char* name) {
    return window.findChild<QLineEdit*>(QString::fromLatin1(name));
}

TEST_F(SettingsWindowTest, WhatWasTypedSurvivesARestart) {
    SettingsWindow window(core::Settings{}, platform::encryptSecret);
    window.findChild<QRadioButton*>(QStringLiteral("useLlm"))->setChecked(true);
    field(window, "endpoint")->setText(QStringLiteral("http://127.0.0.1:11434/v1"));
    field(window, "model")->setText(QStringLiteral("hy-mt2"));
    field(window, "key")->setText(QStringLiteral("sk-秘密-12345"));
    window.findChild<QCheckBox*>(QStringLiteral("verbose"))->setChecked(true);
    save(window);

    // 重新啟動：從檔案讀回來
    const core::Settings reloaded = platform::loadSettings(path()).settings;
    ASSERT_FALSE(reloaded.engines.empty());
    EXPECT_EQ(reloaded.engines[0].id, "openai-compatible");
    EXPECT_EQ(reloaded.engines[0].endpoint, "http://127.0.0.1:11434/v1");
    EXPECT_EQ(reloaded.engines[0].model, "hy-mt2");
    EXPECT_TRUE(reloaded.verboseDiagnostics);
}

TEST_F(SettingsWindowTest, TheKeyIsStoredEncrypted) {
    SettingsWindow window(core::Settings{}, platform::encryptSecret);
    window.findChild<QRadioButton*>(QStringLiteral("useLlm"))->setChecked(true);
    field(window, "key")->setText(QStringLiteral("sk-秘密-12345"));
    save(window);

    // 檔案裡看不到金鑰本身
    std::string contents;
    {
        std::ifstream file(path(), std::ios::binary);
        contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    EXPECT_NE(contents.find("openai-compatible"), std::string::npos) << "設定檔應該寫出來了";
    EXPECT_EQ(contents.find("sk-秘密-12345"), std::string::npos) << "金鑰不該以原文存在檔案裡";

    // 但同一個使用者解得開
    const core::Settings reloaded = platform::loadSettings(path()).settings;
    ASSERT_FALSE(reloaded.engines.empty());
    EXPECT_EQ(platform::decryptSecret(reloaded.engines[0].encryptedApiKey),
              std::optional<std::string>("sk-秘密-12345"));
}

TEST_F(SettingsWindowTest, ReopeningShowsWhatWasSavedButNotTheKey) {
    SettingsWindow first(core::Settings{}, platform::encryptSecret);
    first.findChild<QRadioButton*>(QStringLiteral("useLlm"))->setChecked(true);
    first.findChild<QLineEdit*>(QStringLiteral("model"))->setText(QStringLiteral("hy-mt2"));
    first.findChild<QLineEdit*>(QStringLiteral("key"))->setText(QStringLiteral("sk-秘密-12345"));
    save(first);

    SettingsWindow second(platform::loadSettings(path()).settings, platform::encryptSecret);
    EXPECT_TRUE(second.findChild<QRadioButton*>(QStringLiteral("useLlm"))->isChecked());
    EXPECT_EQ(field(second, "model")->text(), QStringLiteral("hy-mt2"));
    EXPECT_TRUE(field(second, "key")->text().isEmpty()) << "金鑰不會被讀出來顯示";
    EXPECT_EQ(field(second, "key")->echoMode(), QLineEdit::Password);

    // 沒有重填金鑰就存檔，原本的金鑰要留著
    save(second);
    const core::Settings reloaded = platform::loadSettings(path()).settings;
    ASSERT_FALSE(reloaded.engines.empty());
    EXPECT_EQ(platform::decryptSecret(reloaded.engines[0].encryptedApiKey),
              std::optional<std::string>("sk-秘密-12345"));
}

TEST_F(SettingsWindowTest, SwitchingBackToGoogleTakesTheKeyOutOfTheFile) {
    SettingsWindow first(core::Settings{}, platform::encryptSecret);
    first.findChild<QRadioButton*>(QStringLiteral("useLlm"))->setChecked(true);
    field(first, "key")->setText(QStringLiteral("sk-秘密-12345"));
    save(first);

    SettingsWindow second(platform::loadSettings(path()).settings, platform::encryptSecret);
    second.findChild<QRadioButton*>(QStringLiteral("googleOnly"))->setChecked(true);
    save(second);

    const core::Settings reloaded = platform::loadSettings(path()).settings;
    ASSERT_EQ(reloaded.engines.size(), 1u);
    EXPECT_EQ(reloaded.engines[0].id, "google");
    EXPECT_TRUE(reloaded.engines[0].encryptedApiKey.empty());
}

TEST_F(SettingsWindowTest, TheLlmFieldsAreOnlyEnabledForTheLlm) {
    SettingsWindow window(core::Settings{}, platform::encryptSecret);
    EXPECT_FALSE(field(window, "endpoint")->isEnabled()) << "預設是 Google，LLM 的欄位要是灰的";
    window.findChild<QRadioButton*>(QStringLiteral("useLlm"))->setChecked(true);
    EXPECT_TRUE(field(window, "endpoint")->isEnabled());
    EXPECT_TRUE(window.findChild<QCheckBox*>(QStringLiteral("fallback"))->isEnabled());
}

TEST_F(SettingsWindowTest, CancellingChangesNothing) {
    core::Settings settings;
    settings.engines = {{"google", "", "", ""}};
    platform::saveSettings(path(), settings);

    SettingsWindow window(settings, platform::encryptSecret);
    window.findChild<QRadioButton*>(QStringLiteral("useLlm"))->setChecked(true);
    field(window, "key")->setText(QStringLiteral("sk-秘密-12345"));
    bool emitted = false;
    QObject::connect(&window, &SettingsWindow::saved, [&emitted] { emitted = true; });
    window.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();

    EXPECT_FALSE(emitted);
    EXPECT_EQ(platform::loadSettings(path()).settings, settings);
}

TEST_F(SettingsWindowTest, MangaModeSurvivesARestart) {
    SettingsWindow first(core::Settings{}, platform::encryptSecret);
    auto* manga = first.findChild<QCheckBox*>(QStringLiteral("mangaMode"));
    ASSERT_NE(manga, nullptr);
    EXPECT_FALSE(manga->isChecked()) << "預設關閉";
    manga->setChecked(true);
    save(first);

    SettingsWindow second(platform::loadSettings(path()).settings, platform::encryptSecret);
    EXPECT_TRUE(second.findChild<QCheckBox*>(QStringLiteral("mangaMode"))->isChecked());
}

TEST_F(SettingsWindowTest, ClaudeCanBeChosenAsTheLlm) {
    // M2-07：Claude 用它的原生 API，金鑰和 OpenAI 相容的那個分開存
    SettingsWindow first(core::Settings{}, platform::encryptSecret);
    first.findChild<QRadioButton*>(QStringLiteral("useLlm"))->setChecked(true);
    auto* kind = first.findChild<QComboBox*>(QStringLiteral("llmKind"));
    ASSERT_NE(kind, nullptr);
    kind->setCurrentIndex(kind->findData(QStringLiteral("anthropic")));
    field(first, "key")->setText(QStringLiteral("sk-ant-秘密"));
    save(first);

    const core::Settings saved = platform::loadSettings(path()).settings;
    ASSERT_FALSE(saved.engines.empty());
    EXPECT_EQ(saved.engines[0].id, "anthropic");
    EXPECT_EQ(platform::decryptSecret(saved.engines[0].encryptedApiKey),
              std::optional<std::string>("sk-ant-秘密"));

    SettingsWindow second(saved, platform::encryptSecret);
    EXPECT_EQ(second.findChild<QComboBox*>(QStringLiteral("llmKind"))->currentData().toString(),
              QStringLiteral("anthropic"));
}

// M2-10：快捷鍵可以在設定視窗改
TEST_F(SettingsWindowTest, HotkeysShowTheSavedKeysAndSurviveARestart) {
    core::Settings start;
    start.hotkeys.translate = "Ctrl+Alt+Win+F9";
    SettingsWindow first(start, platform::encryptSecret);
    auto* translate = first.findChild<QKeySequenceEdit*>(QStringLiteral("hotkeyTranslate"));
    ASSERT_NE(translate, nullptr);
    // 設定檔的 Win 在 Qt 叫 Meta；Qt 的修飾鍵順序和我們的不同（Meta+Ctrl+Alt+F9），所以比按鍵本身
    EXPECT_EQ(core::parseHotkey(
                  translate->keySequence().toString(QKeySequence::PortableText).toStdString()),
              core::parseHotkey("Ctrl+Alt+Win+F9"));

    first.findChild<QKeySequenceEdit*>(QStringLiteral("hotkeyCapture"))->clear();  // 不使用
    const core::Settings saved = save(first);
    EXPECT_EQ(saved.hotkeys.translate, "Ctrl+Alt+Win+F9");
    EXPECT_EQ(saved.hotkeys.debugDump, "Ctrl+Alt+Shift+D");
    EXPECT_EQ(saved.hotkeys.capture, "");
}

TEST_F(SettingsWindowTest, TheSameHotkeyTwiceIsNotSaved) {
    SettingsWindow window(core::Settings{}, platform::encryptSecret);
    window.findChild<QKeySequenceEdit*>(QStringLiteral("hotkeyCapture"))
        ->setKeySequence(QKeySequence::fromString(QStringLiteral("Ctrl+Alt+Shift+T")));
    bool savedSomething = false;
    QObject::connect(&window, &SettingsWindow::saved, [&] { savedSomething = true; });
    window.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    EXPECT_FALSE(savedSomething) << "和「立即翻譯」重複，不能存";
    auto* problem = window.findChild<QLabel*>(QStringLiteral("hotkeyProblem"));
    ASSERT_NE(problem, nullptr);
    EXPECT_TRUE(problem->text().contains(QStringLiteral("Ctrl+Alt+Shift+T")))
        << problem->text().toStdString();
}

// M2-06：這幾項是目前情境的值
TEST_F(SettingsWindowTest, TheProfileValuesAreEditable) {
    core::Settings start;
    core::switchProfile(start, "game");
    SettingsWindow window(start, platform::encryptSecret);
    auto* edges = window.findChild<QCheckBox*>(QStringLiteral("translateEdges"));
    auto* settle = window.findChild<QSpinBox*>(QStringLiteral("settleMs"));
    ASSERT_NE(edges, nullptr);
    ASSERT_NE(settle, nullptr);
    EXPECT_TRUE(edges->isChecked()) << "遊戲情境預設翻譯碰到邊緣的句子";
    EXPECT_EQ(settle->value(), 600);
    EXPECT_TRUE(window.findChild<QLabel*>(QStringLiteral("profileNote"))
                    ->text()
                    .contains(QStringLiteral("遊戲")));

    settle->setValue(800);
    edges->setChecked(false);
    const core::Settings saved = save(window);
    EXPECT_EQ(saved.settleMs, 800);
    EXPECT_TRUE(saved.dropEdgeBlocks);
    EXPECT_EQ(saved.profile, "game");
}

TEST_F(SettingsWindowTest, OcrLanguageSurvivesARestart) {
    SettingsWindow first(core::Settings{}, platform::encryptSecret);
    auto* language = first.findChild<QComboBox*>(QStringLiteral("ocrLanguage"));
    ASSERT_NE(language, nullptr);
    EXPECT_EQ(language->currentData().toString(), QStringLiteral("auto")) << "預設自動判斷";
    language->setCurrentIndex(language->findData(QStringLiteral("ko")));
    save(first);
    EXPECT_EQ(platform::loadSettings(path()).settings.ocrLanguage, "ko");

    SettingsWindow second(platform::loadSettings(path()).settings, platform::encryptSecret);
    EXPECT_EQ(second.findChild<QComboBox*>(QStringLiteral("ocrLanguage"))->currentData().toString(),
              QStringLiteral("ko"));
}

}  // namespace
}  // namespace tmw::ui
