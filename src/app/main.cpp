#include <windows.h>

// 目前只是專案骨架：確認建置、manifest 和 UTF-8 都正常。
// M0-05 會在這裡建立透鏡視窗和系統匣圖示。
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    MessageBoxW(nullptr, L"專案骨架建置成功。\n透鏡視窗會在 M0-05 實作。",
                L"Translation Magic Window", MB_OK | MB_ICONINFORMATION);
    return 0;
}
