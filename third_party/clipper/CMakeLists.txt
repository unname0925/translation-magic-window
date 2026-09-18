# Clipper 6.4.2（Angus Johnson，Boost Software License 1.0）。
# 用和 PaddleOCR（pyclipper）完全相同的原始碼擴張文字框，C++ 和 Python 的結果才會一致。
# 以本專案的編譯選項建置（包含 ASan），不套用本專案的警告等級。
add_library(tmw_clipper STATIC clipper.cpp clipper.hpp)
target_include_directories(tmw_clipper SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(tmw_clipper PUBLIC cxx_std_20)
target_compile_options(tmw_clipper PRIVATE /W0 /utf-8)
