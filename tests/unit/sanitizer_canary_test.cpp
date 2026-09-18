#include <gtest/gtest.h>

// 警報測試：確認 AddressSanitizer 真的有在運作。
// 故意讀取陣列範圍外的記憶體，ASan 必須抓到並讓程式終止。
// 如果這個測試失敗，代表 ASan 沒有生效，「ASan 測試全部通過」也就沒有意義。
// 只在 ASan 建置中編譯（MSVC 開啟 /fsanitize=address 時會定義 __SANITIZE_ADDRESS__）。
#if defined(TMW_EXPECT_ASAN) && !defined(__SANITIZE_ADDRESS__)
// 編譯器訊息用英文，避免在某些終端機中顯示成亂碼
#error "ASan build, but AddressSanitizer is not enabled. Check cmake/CompilerOptions.cmake."
#endif

#if defined(__SANITIZE_ADDRESS__)

namespace {

int readPastEnd() {
    int* values = new int[4]{};
    volatile int* pointer = values;
    const int result = pointer[4];  // 越界讀取
    delete[] values;
    return result;
}

TEST(SanitizerCanaryDeathTest, DetectsHeapBufferOverflow) {
    EXPECT_DEATH(readPastEnd(), "heap-buffer-overflow");
}

}  // namespace

#endif
