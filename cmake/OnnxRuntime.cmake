# ONNX Runtime（DirectML 版）和 DirectML（見 docs/design.md 4.4、6）。
#
# 這兩個套件不在 vcpkg 中，直接從 NuGet 下載固定版本的官方套件（.nupkg 就是 zip 檔），
# 並用 NuGet 公布的 SHA-512 驗證。下載的檔案放在 .cache/downloads，
# 所有建置資料夾（debug、asan）共用，不會重複下載。
#
# 提供：
#   onnxruntime::onnxruntime        匯入的 DLL 目標（標頭、匯入程式庫）
#   tmw_copy_onnxruntime(<target>)  建置後把執行時需要的 DLL 複製到執行檔旁邊

include(FetchContent)

set(TMW_DOWNLOAD_CACHE "${PROJECT_SOURCE_DIR}/.cache/downloads"
    CACHE PATH "下載的第三方套件存放位置（所有建置資料夾共用）")

# DirectML 版的 ONNX Runtime 停在 1.24.x（DirectML 已進入持續維護，見 design.md 10）
FetchContent_Declare(tmw_onnxruntime
    URL https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/1.24.4/microsoft.ml.onnxruntime.directml.1.24.4.nupkg
    URL_HASH SHA512=6633b2bf8f79be17d55e84c9a76bad4729fc8abd53148bc28f407d24ff106460574b4a05c7e958ed9099a927675528505fa12dc588b64f754eb585dc9814d5e0
    DOWNLOAD_NAME microsoft.ml.onnxruntime.directml.1.24.4.zip
    DOWNLOAD_DIR "${TMW_DOWNLOAD_CACHE}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# ONNX Runtime 1.24.4 的 NuGet 套件指定的 DirectML 版本
FetchContent_Declare(tmw_directml
    URL https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/1.15.4/microsoft.ai.directml.1.15.4.nupkg
    URL_HASH SHA512=fde767f56904abc90fd53f65d8729c918ab7f6e3c5e1ecdd479908fc02b4535cf2b0860f7ab2acb9b731d6cb809b72c3d5d4d02853fb8f5ea022a47bc44ef285
    DOWNLOAD_NAME microsoft.ai.directml.1.15.4.zip
    DOWNLOAD_DIR "${TMW_DOWNLOAD_CACHE}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(tmw_onnxruntime tmw_directml)

set(_tmw_ort_native "${tmw_onnxruntime_SOURCE_DIR}/runtimes/win-x64/native")
add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION "${_tmw_ort_native}/onnxruntime.dll"
    IMPORTED_IMPLIB "${_tmw_ort_native}/onnxruntime.lib"
    # dml_provider_factory.h 會引用 DirectML.h
    INTERFACE_INCLUDE_DIRECTORIES
        "${tmw_onnxruntime_SOURCE_DIR}/build/native/include;${tmw_directml_SOURCE_DIR}/include")

# 執行時需要放在執行檔旁邊的 DLL。DirectML.dll 要用套件附的版本（1.15.4）：
# Windows 內建的 DirectML.dll 可能比 ONNX Runtime 需要的版本舊。
set(TMW_ONNXRUNTIME_DLLS
    "${_tmw_ort_native}/onnxruntime.dll"
    "${_tmw_ort_native}/onnxruntime_providers_shared.dll"
    "${tmw_directml_SOURCE_DIR}/bin/x64-win/DirectML.dll")
foreach(dll IN LISTS TMW_ONNXRUNTIME_DLLS)
    if(NOT EXISTS "${dll}")
        message(FATAL_ERROR "ONNX Runtime 套件中找不到 ${dll}")
    endif()
endforeach()

function(tmw_copy_onnxruntime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TMW_ONNXRUNTIME_DLLS}
                "$<TARGET_FILE_DIR:${target}>"
        VERBATIM)
endfunction()
