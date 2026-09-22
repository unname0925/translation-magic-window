// 故意當掉的小程式，只給整合測試用（M1-14 的驗收條件「故意觸發當機會產生 dump」）。
//
// 裝上當機傾印的處理常式，然後寫入空指標。正式程式不會有這種東西，所以用一個
// 獨立的執行檔，而不是在主程式裡留一個「讓我當掉」的指令。
#include <windows.h>

#include <filesystem>

#include "platform/crash_dump.h"

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        return 2;
    }
    // 測試是無人看管的：不要跳出 Windows 的錯誤回報對話框，不然會一直卡在那裡
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!tmw::platform::installCrashHandler(std::filesystem::path(argv[1]))) {
        return 3;
    }

    volatile int* nowhere = nullptr;
    *nowhere = 1;  // 存取違規
    return 0;
}
