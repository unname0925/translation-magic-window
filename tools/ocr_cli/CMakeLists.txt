# tmw_ocr_cli：對圖片跑 OCR，輸出 JSON 和各步驟的耗時。
# 用來和 Python 參考版（tools/eval/ocr_reference.py）比對結果，並量測 GPU／CPU 的速度。
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(tmw_ocr_cli main.cpp)
target_link_libraries(tmw_ocr_cli PRIVATE tmw_ocr tmw_platform tmw_options nlohmann_json::nlohmann_json)
tmw_copy_onnxruntime(tmw_ocr_cli)
tmw_copy_asan_runtime(tmw_ocr_cli)
