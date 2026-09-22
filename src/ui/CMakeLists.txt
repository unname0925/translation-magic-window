# ui：Qt 的視窗（見 docs/design.md 4.6、4.7）。Qt 是動態連結，DLL 由 tmw_copy_qt 複製。
# Q_OBJECT 的類別要經過 moc
set(CMAKE_AUTOMOC ON)

add_library(tmw_ui STATIC
    card_delegate.cpp
    card_delegate.h
    card_text.cpp
    card_text.h
    history_model.cpp
    history_model.h
    qt_runtime.cpp
    qt_runtime.h
    result_window.cpp
    result_window.h
    ruby_text.cpp
    ruby_text.h)
target_include_directories(tmw_ui PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tmw_ui
    PUBLIC Qt6::Widgets tmw_core
    PRIVATE tmw_options)
