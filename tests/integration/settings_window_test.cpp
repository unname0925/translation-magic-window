// 設定視窗（M1-13）。驗收條件是「設定儲存後重新啟動，內容還在，且金鑰是加密的」，
// 所以這裡走完整條路：在畫面上填東西 → 存檔 → 重新讀檔 → 檢查內容和金鑰。
#include "ui/settings_window.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>

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

}  // namespace
}  // namespace tmw::ui
