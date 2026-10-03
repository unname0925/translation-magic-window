# tmw_web_host：Chrome／Edge 的 Native Messaging 主機，在瀏覽器和主程式的具名管道之間轉送封包。
# 主控台程式（用標準輸入／輸出），瀏覽器啟動它時不會顯示視窗。
add_executable(tmw_web_host main.cpp)
target_include_directories(tmw_web_host PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tmw_web_host PRIVATE tmw_platform tmw_core tmw_options)
tmw_copy_asan_runtime(tmw_web_host)
