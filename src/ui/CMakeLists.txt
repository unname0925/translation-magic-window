# ui：Qt 的視窗（見 docs/design.md 4.6、4.7）。Qt 是動態連結，DLL 由 tmw_copy_qt 複製。
add_library(tmw_ui STATIC
    qt_runtime.cpp
    qt_runtime.h)
target_include_directories(tmw_ui PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tmw_ui
    PUBLIC Qt6::Widgets
    PRIVATE tmw_options)
