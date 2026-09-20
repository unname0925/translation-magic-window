// 確認 Qt 的建置設定和執行檔旁邊的 DLL 是同一套（M1-01）。
#include "ui/qt_runtime.h"

#include <gtest/gtest.h>

namespace tmw::ui {
namespace {

TEST(QtRuntimeTest, RuntimeVersionMatchesTheCompiledOne) {
    EXPECT_FALSE(compiledQtVersion().empty());
    EXPECT_EQ(runtimeQtVersion(), compiledQtVersion());
}

}  // namespace
}  // namespace tmw::ui
