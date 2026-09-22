# 只在整合測試中執行：裝上當機傾印的處理常式之後故意當掉
add_executable(tmw_crash_helper main.cpp)
target_link_libraries(tmw_crash_helper PRIVATE tmw_platform tmw_options)
set_target_properties(tmw_crash_helper PROPERTIES OUTPUT_NAME "tmw_crash_helper")
