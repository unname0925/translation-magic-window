#include <windows.h>

#include <gtest/gtest.h>
#include <winrt/base.h>

int main(int argc, char** argv) {
    // 和主程式一樣使用實體像素座標
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
