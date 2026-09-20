# 找到 vcpkg 的 CMake 工具鏈。必須在 project() 之前 include。
#
# 依序使用：
# 1. 已經指定的 CMAKE_TOOLCHAIN_FILE
# 2. 環境變數 VCPKG_ROOT
# 3. Visual Studio（含 Build Tools）內建的 vcpkg：用 vswhere 找到安裝位置，
#    這樣在 VS Code 裡不需要另外設定環境變數
#
# 相依套件列在 vcpkg.json（manifest 模式），第一次設定（configure）時會自動下載並建置。

# 相依套件一律靜態連結、但使用動態的 C 執行階段（/MD）：
# - 發布時不必散布 OpenCV、curl 等 DLL（ONNX Runtime 和 DirectML 仍然是 DLL）
# - OpenCC 依賴的 marisa-trie 在 Windows 上只支援靜態連結
if(NOT DEFINED VCPKG_TARGET_TRIPLET AND NOT DEFINED ENV{VCPKG_TARGET_TRIPLET})
    set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "vcpkg 的目標 triplet")
endif()

if(DEFINED CMAKE_TOOLCHAIN_FILE)
    return()
endif()

if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    return()
endif()

set(_tmw_program_files_x86 "ProgramFiles(x86)")
set(_tmw_vswhere "$ENV{${_tmw_program_files_x86}}/Microsoft Visual Studio/Installer/vswhere.exe")
if(EXISTS "${_tmw_vswhere}")
    execute_process(
        COMMAND "${_tmw_vswhere}" -latest -products * -requires Microsoft.VisualStudio.Component.Vcpkg
                -property installationPath
        OUTPUT_VARIABLE _tmw_vs_path
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_tmw_vs_path AND EXISTS "${_tmw_vs_path}/VC/vcpkg/scripts/buildsystems/vcpkg.cmake")
        file(TO_CMAKE_PATH "${_tmw_vs_path}/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
             CMAKE_TOOLCHAIN_FILE)
        return()
    endif()
endif()

message(FATAL_ERROR
    "找不到 vcpkg。請在 Visual Studio Installer 中安裝「vcpkg 套件管理員」元件，"
    "或把環境變數 VCPKG_ROOT 設成 vcpkg 的資料夾。")
