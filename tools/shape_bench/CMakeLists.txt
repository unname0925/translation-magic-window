# tmw_shape_bench：量測同一個工作階段在不同輸入形狀下的耗時（M1-03 的效能調查）。
add_executable(tmw_shape_bench main.cpp)
target_link_libraries(tmw_shape_bench PRIVATE tmw_ocr tmw_options onnxruntime::onnxruntime)
tmw_copy_onnxruntime(tmw_shape_bench)
tmw_copy_asan_runtime(tmw_shape_bench)
