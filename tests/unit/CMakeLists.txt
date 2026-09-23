# 單元測試：core，以及不需要桌面環境的 platform 工具函式。
add_executable(tmw_unit_tests
    core/auto_trigger_test.cpp
    core/change_detection_test.cpp
    core/clock_test.cpp
    core/command_line_test.cpp
    core/debug_overlay_test.cpp
    core/debug_report_test.cpp
    core/geometry_test.cpp
    core/history_test.cpp
    core/language_test.cpp
    core/lens_layout_test.cpp
    core/opencc_converter_test.cpp
    core/perf_stats_test.cpp
    core/pipeline_test.cpp
    core/pipeline_worker_test.cpp
    core/ruby_test.cpp
    core/settings_test.cpp
    core/text_layout_test.cpp
    core/translation_alignment_test.cpp
    core/translation_cache_test.cpp
    core/translation_service_test.cpp
    core/translation_stream_test.cpp
    core/translator_chain_test.cpp
    core/trigger_state_machine_test.cpp
    core/utf8_test.cpp
    net/cpr_http_client_test.cpp
    net/google_translator_test.cpp
    net/openai_translator_test.cpp
    net/rate_limiter_test.cpp
    ocr/model_choice_test.cpp
    ocr/model_config_test.cpp
    ocr/text_detector_test.cpp
    ocr/text_recognizer_test.cpp
    platform/logging_test.cpp
    platform/png_file_test.cpp
    platform/secret_test.cpp
    platform/settings_file_test.cpp
    platform/crash_dump_test.cpp
    platform/debug_dump_test.cpp
    platform/single_instance_test.cpp
    platform/text_encoding_test.cpp
    platform/win_error_test.cpp
    ui/engine_choice_test.cpp
    ui/history_model_test.cpp
    ui/qt_runtime_test.cpp
    ui/ruby_text_test.cpp
    sanitizer_canary_test.cpp)
target_include_directories(tmw_unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
# OpenCC 的字典檔不必複製到測試旁邊，直接讀 vcpkg 安裝的位置（見 cmake/Opencc.cmake）
target_compile_definitions(tmw_unit_tests PRIVATE
    TMW_OPENCC_DATA_DIR="${TMW_OPENCC_DATA_DIR}"
    # 錄好的 HTTP 回應（CT-01、CT-02）
    TMW_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
target_link_libraries(tmw_unit_tests PRIVATE
    tmw_core
    tmw_net
    tmw_ocr
    tmw_platform
    tmw_ui
    tmw_options
    GTest::gtest_main)
if(TMW_ENABLE_ASAN)
    # 讓 sanitizer_canary_test.cpp 能確認 ASan 真的有生效
    target_compile_definitions(tmw_unit_tests PRIVATE TMW_EXPECT_ASAN)
endif()
tmw_copy_asan_runtime(tmw_unit_tests)
tmw_copy_onnxruntime(tmw_unit_tests)
tmw_copy_qt(tmw_unit_tests)

# PRE_TEST：在執行測試時才列出測試案例，避免建置時就需要 ASan DLL。
# DISCOVERY_TIMEOUT：防毒軟體（例如 Norton）第一次執行新建置的執行檔時，
# 會先掃描 20 秒以上；預設的 5 秒不夠，會誤判成測試卡住。
gtest_discover_tests(tmw_unit_tests
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
