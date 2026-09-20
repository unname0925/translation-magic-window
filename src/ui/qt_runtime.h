// ui：Qt 的視窗（結果視窗、設定視窗）。第一階段只有這個檔案，確認 Qt 的建置和執行環境一致。
#pragma once

#include <string>

namespace tmw::ui {

// 建置時使用的 Qt 版本（來自標頭檔）
std::string compiledQtVersion();

// 執行時載入的 Qt 版本（來自 DLL）。和上面不同表示執行檔旁邊的 Qt DLL 不是建置時那一套。
std::string runtimeQtVersion();

}  // namespace tmw::ui
