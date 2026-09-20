cmake_minimum_required(VERSION 3.28)

# vcpkg 必須在 project() 之前設定（相依套件列在 vcpkg.json）
include("${CMAKE_CURRENT_LIST_DIR}/cmake/Vcpkg.cmake")

project(TranslationMagicWindow
    VERSION 0.1.0
    DESCRIPTION "把畫面上的日文、英文、韓文翻譯成繁體中文的透鏡工具"
    LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "本專案目前只支援 MSVC（Visual Studio Build Tools）。")
endif()

option(TMW_BUILD_TESTS "建置測試" ON)
option(TMW_WARNINGS_AS_ERRORS "把編譯警告當成錯誤" ON)
option(TMW_ENABLE_ASAN "啟用 AddressSanitizer" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)

# 所有執行檔和之後的 DLL（ONNX Runtime 等）都放在同一個資料夾
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompilerOptions)
include(OnnxRuntime)
include(Qt)

add_subdirectory(third_party/clipper)
add_subdirectory(src)
add_subdirectory(tools)

if(TMW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
