# 整合測試：需要桌面環境，執行時會在螢幕左上角短暫顯示測試視窗。
# 標記為 integration，一般的測試流程（debug、release、asan）不會執行，
# 請用 cmake --workflow --preset integration。
add_executable(tmw_integration_tests
    main.cpp
    screen_capture_test.cpp
    support/test_window.cpp
    support/test_window.h)
target_include_directories(tmw_integration_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tmw_integration_tests PRIVATE
    tmw_platform
    tmw_core
    tmw_options
    dwmapi
    GTest::gtest)
tmw_copy_asan_runtime(tmw_integration_tests)

gtest_discover_tests(tmw_integration_tests
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120
    PROPERTIES LABELS integration)
