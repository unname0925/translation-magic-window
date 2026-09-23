# tmw_capture_bench：量測閒置時的 CPU 使用率（M1-17）。需要桌面環境。
add_executable(tmw_capture_bench main.cpp)
target_link_libraries(tmw_capture_bench PRIVATE tmw_platform tmw_core tmw_options)
tmw_copy_asan_runtime(tmw_capture_bench)
