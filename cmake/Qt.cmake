# 找到 Qt 6（結果視窗和設定視窗用 Qt Widgets，動態連結，見 design.md 6）。
#
# Qt 不透過 vcpkg 取得：vcpkg 會從原始碼建置整個 Qt，本機要 30～60 分鐘、CI 要 1～3 小時，
# 而官方預編譯的 qtbase 只有 39 MB、半分鐘就裝好。安裝方式見 README.md。
#
# 依序尋找：
# 1. 已經指定的 QT_ROOT 或 CMAKE_PREFIX_PATH
# 2. 環境變數 QT_ROOT
# 3. 本專案的預設位置 .cache/qt/<版本>/msvc2022_64（README 的指令會裝在這裡）
# 4. 系統上已安裝的 Qt（由 find_package 自己尋找）

set(TMW_QT_VERSION "6.9.3" CACHE STRING "使用的 Qt 版本（和 README、CI 的安裝指令一致）")

if(NOT DEFINED QT_ROOT AND DEFINED ENV{QT_ROOT})
    set(QT_ROOT "$ENV{QT_ROOT}")
endif()

if(NOT DEFINED QT_ROOT)
    set(_tmw_qt_default "${PROJECT_SOURCE_DIR}/.cache/qt/${TMW_QT_VERSION}/msvc2022_64")
    if(EXISTS "${_tmw_qt_default}/lib/cmake/Qt6/Qt6Config.cmake")
        set(QT_ROOT "${_tmw_qt_default}")
    endif()
endif()

if(DEFINED QT_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
endif()

find_package(Qt6 ${TMW_QT_VERSION} COMPONENTS Widgets)

if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "找不到 Qt ${TMW_QT_VERSION}。安裝方式（見 README.md「安裝 Qt」）：\n"
        "  pip install aqtinstall==3.3.0\n"
        "  aqt install-qt windows desktop ${TMW_QT_VERSION} win64_msvc2022_64 "
        "--archives qtbase --outputdir .cache/qt\n"
        "已經裝在別的位置時，把 QT_ROOT 設成該資料夾（例如 C:/Qt/${TMW_QT_VERSION}/msvc2022_64）。")
endif()

# Qt 的 DLL 要放在執行檔旁邊，這樣不管從 ctest、VS Code 還是直接點兩下都能執行
function(tmw_copy_qt target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE:Qt6::Gui>" "$<TARGET_FILE:Qt6::Widgets>"
            "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
            "$<TARGET_FILE_DIR:${target}>/platforms"
        VERBATIM)
endfunction()
