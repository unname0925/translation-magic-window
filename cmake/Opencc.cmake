# OpenCC：把譯文轉成台灣繁體（見 docs/design.md 4.5）。
#
# vcpkg 只安裝程式庫和字典檔，沒有提供部署用的 CMake 變數，所以這裡從標頭資料夾推回安裝位置。
#
# 提供：
#   TMW_OPENCC_DATA_DIR       字典檔在建置機器上的位置（測試直接讀這裡）
#   tmw_copy_opencc(<target>) 建置後把字典複製到執行檔旁邊的 opencc\

find_package(OpenCC CONFIG REQUIRED)

# vcpkg 的 opencc 在相依目標中列了「RapidJSON」，但 rapidjson 只有標頭檔、沒有程式庫，
# vcpkg 自己的目標也叫小寫的 rapidjson。不補上這個名稱的話，連結器會去找不存在的 RapidJSON.lib。
if(NOT TARGET RapidJSON)
    add_library(RapidJSON INTERFACE IMPORTED GLOBAL)
endif()

# 靜態版的 opencc 也忘了定義 Opencc_BUILT_AS_STATIC，少了它，Export.hpp 會把函式宣告成
# __declspec(dllimport)，連結時就找不到 __imp_ 開頭的符號。
get_target_property(_tmw_opencc_type OpenCC::OpenCC TYPE)
if(_tmw_opencc_type STREQUAL "STATIC_LIBRARY")
    set_property(TARGET OpenCC::OpenCC APPEND PROPERTY
        INTERFACE_COMPILE_DEFINITIONS Opencc_BUILT_AS_STATIC)
endif()

get_filename_component(TMW_OPENCC_DATA_DIR "${OPENCC_INCLUDE_DIR}/../../share/opencc" ABSOLUTE)

# s2twp 這條轉換鏈用到的檔案。其餘字典（日文新字體、香港字）用不到，不必跟著散佈。
set(TMW_OPENCC_FILES
    s2twp.json
    STPhrases.ocd2
    STCharacters.ocd2
    TWPhrases.ocd2
    TWVariants.ocd2)

set(TMW_OPENCC_PATHS "")
foreach(file IN LISTS TMW_OPENCC_FILES)
    if(NOT EXISTS "${TMW_OPENCC_DATA_DIR}/${file}")
        message(FATAL_ERROR "找不到 OpenCC 的字典檔 ${TMW_OPENCC_DATA_DIR}/${file}")
    endif()
    list(APPEND TMW_OPENCC_PATHS "${TMW_OPENCC_DATA_DIR}/${file}")
endforeach()

function(tmw_copy_opencc target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/opencc"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TMW_OPENCC_PATHS}
                "$<TARGET_FILE_DIR:${target}>/opencc"
        VERBATIM)
endfunction()
