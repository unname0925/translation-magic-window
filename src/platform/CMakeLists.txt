# platform：把 Windows API 包裝成 core 使用的形式，盡量保持精簡。
add_library(tmw_platform STATIC
    app_paths.cpp
    app_paths.h
    capture_frame_source.cpp
    capture_frame_source.h
    crash_dump.cpp
    crash_dump.h
    debug_dump.cpp
    debug_dump.h
    lens_window.cpp
    lens_window.h
    logging.cpp
    logging.h
    png_file.cpp
    png_file.h
    screen_capture.cpp
    screen_capture.h
    secret.cpp
    secret.h
    settings_file.cpp
    settings_file.h
    single_instance.cpp
    single_instance.h
    text_encoding.cpp
    text_encoding.h
    tray_icon.cpp
    tray_icon.h
    win_error.cpp
    win_error.h)
target_include_directories(tmw_platform PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(tmw_platform PUBLIC cxx_std_20)
find_package(spdlog CONFIG REQUIRED)
target_link_libraries(tmw_platform
    PUBLIC tmw_core user32 gdi32 shell32 ole32 d3d11 dxgi windowscodecs windowsapp crypt32 dbghelp
    PRIVATE spdlog::spdlog tmw_options)
