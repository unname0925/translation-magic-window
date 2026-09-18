#pragma once

#include <windows.h>

// 從程式外部辨識和操作執行中的主程式所需的名稱與指令。
// 第二個執行個體和整合測試（tests/integration/app_test.cpp）都靠這些值找到主程式；
// 兩邊都引用這個檔案，所以不會對不上。
namespace tmw::app {

// 單一執行個體用的具名 mutex
inline constexpr wchar_t kInstanceMutexName[] = L"Local\\TranslationMagicWindow.Instance";

// 看不見的主控視窗的類別名稱
inline constexpr wchar_t kControllerClassName[] = L"TranslationMagicWindow.Controller";

// 主控視窗接受的 WM_COMMAND（系統匣選單使用；整合測試也用它來操作主程式）
enum Command : UINT {
    kCommandToggleLens = 1,
    kCommandExit = 2,
    kCommandCapture = 3,
    kCommandOpenCaptures = 4,
    kCommandToggleAutoSave = 5,
};

}  // namespace tmw::app
