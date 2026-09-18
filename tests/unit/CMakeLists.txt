# 單元測試：core，以及不需要桌面環境的 platform 工具函式。
add_executable(tmw_unit_tests
    core/auto_trigger_test.cpp
    core/change_detection_test.cpp
    core/clock_test.cpp
    core/geometry_test.cpp
    core/lens_layout_test.cpp
    core/trigger_state_machine_test.cpp
    platform/png_file_test.cpp
    platform/single_instance_test.cpp
    platform/text_encoding_test.cpp
    platform/win_error_test.cpp
    sanitizer_canary_test.cpp)
target_include_directories(tmw_unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tmw_unit_tests PRIVATE
    tmw_core
    tmw_platform
    tmw_options
    GTest::gtest_main)
if(TMW_ENABLE_ASAN)
    # 讓 sanitizer_canary_test.cpp 能確認 ASan 真的有生效
    target_compile_definitions(tmw_unit_tests PRIVATE TMW_EXPECT_ASAN)
endif()
tmw_copy_asan_runtime(tmw_unit_tests)

# PRE_TEST：在執行測試時才列出測試案例，避免建置時就需要 ASan DLL。
# DISCOVERY_TIMEOUT：防毒軟體（例如 Norton）第一次執行新建置的執行檔時，
# 會先掃描 20 秒以上；預設的 5 秒不夠，會誤判成測試卡住。
gtest_discover_tests(tmw_unit_tests
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
