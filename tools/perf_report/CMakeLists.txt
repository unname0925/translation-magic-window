# tmw_perf_report：量測每個步驟的耗時，並對照 design.md 第 5 節的預算（M1-15）。
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(tmw_perf_report main.cpp)
target_link_libraries(tmw_perf_report PRIVATE tmw_ocr tmw_core tmw_platform tmw_options nlohmann_json::nlohmann_json)
tmw_copy_onnxruntime(tmw_perf_report)
tmw_copy_asan_runtime(tmw_perf_report)
