#include "core/hotkey.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace tmw::core {
namespace {

TEST(HotkeyTest, ReadsTheUsualForm) {
    const std::optional<Hotkey> hotkey = parseHotkey("Ctrl+Alt+Shift+T");
    ASSERT_TRUE(hotkey.has_value());
    EXPECT_TRUE(hotkey->ctrl && hotkey->alt && hotkey->shift);
    EXPECT_FALSE(hotkey->win);
    EXPECT_EQ(hotkey->key, "T");
}

TEST(HotkeyTest, OrderCaseAndSpacesDoNotMatter) {
    EXPECT_EQ(parseHotkey(" shift + ctrl + t "), parseHotkey("Ctrl+Shift+T"));
    EXPECT_EQ(formatHotkey(*parseHotkey("shift+alt+ctrl+t")), "Ctrl+Alt+Shift+T");
}

TEST(HotkeyTest, MetaIsTheWindowsKey) {
    // Qt 的按鍵錄製欄位把 Windows 鍵寫成 Meta
    EXPECT_EQ(formatHotkey(*parseHotkey("Meta+Alt+1")), "Alt+Win+1");
}

TEST(HotkeyTest, FunctionKeysMayStandAlone) {
    EXPECT_EQ(formatHotkey(*parseHotkey("F9")), "F9");
    EXPECT_EQ(formatHotkey(*parseHotkey("Shift+F12")), "Shift+F12");
    EXPECT_FALSE(parseHotkey("F25").has_value());
    EXPECT_FALSE(parseHotkey("F0").has_value());
}

TEST(HotkeyTest, RejectsKeysThatWouldStealTyping) {
    // 單按 T 或 Shift+T：在別的程式裡就打不出那個字了
    EXPECT_FALSE(parseHotkey("T").has_value());
    EXPECT_FALSE(parseHotkey("Shift+T").has_value());
}

TEST(HotkeyTest, RejectsWhatItCannotRead) {
    EXPECT_FALSE(parseHotkey("").has_value());
    EXPECT_FALSE(parseHotkey("Ctrl+Alt").has_value()) << "只有修飾鍵";
    EXPECT_FALSE(parseHotkey("Ctrl+T+Alt").has_value()) << "按鍵要在最後";
    EXPECT_FALSE(parseHotkey("Ctrl+Space").has_value());
    EXPECT_FALSE(parseHotkey("Ctrl+").has_value());
}

TEST(HotkeyProblemTest, AValidSetHasNoProblem) {
    const std::vector<std::string> names{"立即翻譯", "除錯傾印"};
    EXPECT_EQ(hotkeyProblem(names, std::vector<std::string>{"Ctrl+Alt+T", "Ctrl+Alt+D"}), "");
}

TEST(HotkeyProblemTest, NamesTheBadOne) {
    const std::vector<std::string> names{"立即翻譯", "除錯傾印"};
    const std::string problem = hotkeyProblem(names, std::vector<std::string>{"Ctrl+Alt+T", "D"});
    EXPECT_NE(problem.find("除錯傾印"), std::string::npos) << problem;
}

TEST(HotkeyProblemTest, CatchesTheSameKeyTwice) {
    const std::vector<std::string> names{"立即翻譯", "除錯傾印"};
    const std::string problem =
        hotkeyProblem(names, std::vector<std::string>{"Ctrl+Alt+T", "alt+ctrl+t"});
    EXPECT_NE(problem.find("都是 Ctrl+Alt+T"), std::string::npos) << problem;
}

TEST(HotkeyProblemTest, AnEmptyHotkeyMeansUnused) {
    const std::vector<std::string> names{"立即翻譯", "除錯傾印"};
    EXPECT_EQ(hotkeyProblem(names, std::vector<std::string>{"", ""}), "");
}

}  // namespace
}  // namespace tmw::core
