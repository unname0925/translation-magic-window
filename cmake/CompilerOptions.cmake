# 本專案共用的編譯選項。透過 tmw_options 套用，不影響第三方套件。
add_library(tmw_options INTERFACE)
target_compile_features(tmw_options INTERFACE cxx_std_20)
target_compile_options(tmw_options INTERFACE
    /W4
    /permissive-
    /utf-8
    /Zc:__cplusplus
    # 標準的例外處理模型。Visual Studio 產生器會自動加上，Ninja 不會，
    # 所以明確指定，讓兩種產生器（本機和 CI）用相同的旗標。
    /EHsc
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
    WIN32_LEAN_AND_MEAN
    # 除錯傾印和使用者回報問題時要看得出是哪一版（design.md 4.12）
    TMW_VERSION="${PROJECT_VERSION}")

# AddressSanitizer 必須套用在所有目標（包含 GoogleTest），
# 否則 MSVC 會因為 STL 容器的 ASan 標註不一致而連結失敗。
if(TMW_ENABLE_ASAN)
    add_compile_options(/fsanitize=address)
    # vcpkg 的靜態程式庫（spdlog、OpenCV…）沒有開 ASan。STL 的容器標註只要兩邊不一致，
    # 連結就會失敗（LNK2038 annotate_string/vector/optional）。關掉容器標註後，
    # 堆積、堆疊、use-after-free 都照常偵測，只是不檢查 vector、string 內部的越界。
    #
    # 四個巨集都要定義：_DISABLE_STL_ANNOTATION 是新版 STL 才有的總開關（VS 2026），
    # CI 用的 VS 2022 只認得個別的那三個。定義了不存在的巨集沒有副作用。
    add_compile_definitions(_DISABLE_STL_ANNOTATION _DISABLE_STRING_ANNOTATION
                            _DISABLE_VECTOR_ANNOTATION _DISABLE_OPTIONAL_ANNOTATION)
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
