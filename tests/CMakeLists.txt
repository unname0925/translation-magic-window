include(FetchContent)

# 固定 GoogleTest 版本並驗證雜湊值，確保每台電腦建置出來的結果都一樣
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.zip
    URL_HASH SHA256=63b9c77751a5b8f492486005f67533fdc58682b67476fcb91650be5958d5195a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)
# 和本專案使用相同的 C 執行階段（/MD）
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_subdirectory(unit)
