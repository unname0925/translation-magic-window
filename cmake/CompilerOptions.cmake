# 本專案共用的編譯選項。透過 tmw_options 套用，不影響第三方套件。
add_library(tmw_options INTERFACE)
target_compile_features(tmw_options INTERFACE cxx_std_20)
target_compile_options(tmw_options INTERFACE
    /W4
    /permissive-
    /utf-8
    /Zc:__cplusplus
    /MP
    # C++/WinRT 標頭很大，需要更多的 section
    /bigobj
    # 以角括號引入的標頭（Windows SDK、C++/WinRT）視為外部程式碼，不檢查警告
    /external:anglebrackets
    /external:W0
    $<$<BOOL:${TMW_WARNINGS_AS_ERRORS}>:/WX>)
target_compile_definitions(tmw_options INTERFACE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN)

# AddressSanitizer 必須套用在所有目標（包含 GoogleTest），
# 否則 MSVC 會因為 STL 容器的 ASan 標註不一致而連結失敗。
if(TMW_ENABLE_ASAN)
    add_compile_options(/fsanitize=address)
    # /RTC 和 /INCREMENTAL 都和 ASan 不相容
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/INCREMENTAL" "/INCREMENTAL:NO" CMAKE_EXE_LINKER_FLAGS_DEBUG
        "${CMAKE_EXE_LINKER_FLAGS_DEBUG}")
endif()

# ASan 版的執行檔需要 ASan 執行階段 DLL，把它複製到執行檔旁邊，
# 這樣不管從 ctest、VS Code 還是直接點兩下都能執行。
function(tmw_copy_asan_runtime target)
    if(NOT TMW_ENABLE_ASAN)
        return()
    endif()
    get_filename_component(compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(dll clang_rt.asan_dynamic-x86_64.dll clang_rt.asan_dbg_dynamic-x86_64.dll)
        if(EXISTS "${compiler_dir}/${dll}")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${compiler_dir}/${dll}" "$<TARGET_FILE_DIR:${target}>")
        endif()
    endforeach()
endfunction()
