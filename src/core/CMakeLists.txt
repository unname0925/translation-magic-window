# core：純邏輯，不依賴 Win32、D3D、Qt 或網路（見 docs/design.md 3.2）。
# 請不要在這裡加入任何 Windows 專屬的相依套件。
add_library(tmw_core STATIC
    auto_trigger.cpp
    auto_trigger.h
    change_detection.cpp
    change_detection.h
    clock.h
    command_line.cpp
    command_line.h
    color.h
    frame_source.h
    geometry.cpp
    geometry.h
    image.h
    language.cpp
    language.h
    lens_layout.cpp
    lens_layout.h
    opencc_converter.cpp
    opencc_converter.h
    settings.cpp
    settings.h
    text_converter.h
    text_layout.cpp
    text_layout.h
    translation_alignment.cpp
    translation_alignment.h
    translation_cache.cpp
    translation_cache.h
    translation_service.cpp
    translation_service.h
    translator.cpp
    translator.h
    translator_chain.cpp
    translator_chain.h
    trigger_state_machine.cpp
    trigger_state_machine.h
    utf8.cpp
    utf8.h)
target_include_directories(tmw_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(tmw_core PUBLIC cxx_std_20)
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(tmw_core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE OpenCC::OpenCC tmw_options)
