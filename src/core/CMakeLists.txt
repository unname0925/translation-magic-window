# core：純邏輯，不依賴 Win32、D3D、Qt 或網路（見 docs/design.md 3.2）。
# 請不要在這裡加入任何 Windows 專屬的相依套件。
add_library(tmw_core STATIC
    clock.h
    geometry.cpp
    geometry.h)
target_include_directories(tmw_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(tmw_core PUBLIC cxx_std_20)
target_link_libraries(tmw_core PRIVATE tmw_options)
