#include <windows.h>

#include <gtest/gtest.h>
#include <winrt/base.h>

#include <QApplication>

int main(int argc, char** argv) {
    // 和主程式一樣使用實體像素座標
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    // 結果視窗是 Qt 的。這裡只是建立起來，事件迴圈由測試自己推動。
    const QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
