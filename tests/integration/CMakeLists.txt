# 整合測試：需要桌面環境。執行時會短暫顯示測試視窗、啟動主程式，並移動滑鼠
# （結束後放回原位）。標記為 integration，一般的測試流程（debug、release、asan）
# 不會執行，請用 cmake --workflow --preset integration。
add_executable(tmw_integration_tests
    app_test.cpp
    main.cpp
    pipeline_test.cpp
    result_window_test.cpp
    settings_window_test.cpp
    screen_capture_test.cpp
    support/app_process.cpp
    support/app_process.h
    support/mouse_input.cpp
    support/mouse_input.h
    support/test_window.cpp
    support/test_window.h)
target_include_directories(tmw_integration_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tmw_integration_tests PRIVATE
    tmw_platform
    tmw_ocr
    tmw_ui
    tmw_core
    tmw_options
    dwmapi
    GTest::gtest)
tmw_copy_asan_runtime(tmw_integration_tests)
tmw_copy_onnxruntime(tmw_integration_tests)
tmw_copy_qt(tmw_integration_tests)
# app_test.cpp 會啟動同一個資料夾中的主程式，建置測試時要一併建置它
add_dependencies(tmw_integration_tests tmw_app)

# TIMEOUT：單一測試超過 60 秒就判定失敗，真的卡住時不會無限等待
gtest_discover_tests(tmw_integration_tests
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120
    PROPERTIES LABELS integration TIMEOUT 60)
