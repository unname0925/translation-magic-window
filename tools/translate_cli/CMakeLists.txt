# tmw_translate_cli：對真正的端點實測翻譯引擎（單元測試用的是錄好的回應）。
add_executable(tmw_translate_cli main.cpp)
target_link_libraries(tmw_translate_cli PRIVATE tmw_net tmw_platform tmw_options)
tmw_copy_asan_runtime(tmw_translate_cli)
