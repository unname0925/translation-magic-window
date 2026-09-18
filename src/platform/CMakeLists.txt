# platform：把 Windows API 包裝成 core 使用的形式，盡量保持精簡。
add_library(tmw_platform STATIC
    text_encoding.cpp
    text_encoding.h)
target_include_directories(tmw_platform PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(tmw_platform PUBLIC cxx_std_20)
target_link_libraries(tmw_platform
    PUBLIC tmw_core
    PRIVATE tmw_options)
