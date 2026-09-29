# ocr：ONNX Runtime 推論，以及 PP-OCR 的前後處理（見 docs/design.md 4.4）。
find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(tmw_ocr STATIC
    comic_text_detector.cpp
    comic_text_detector.h
    manga_ocr.cpp
    manga_ocr.h
    model_choice.cpp
    model_choice.h
    model_config.cpp
    model_config.h
    ocr_pipeline.cpp
    ocr_pipeline.h
    ocr_service.cpp
    ocr_service.h
    onnx_internal.h
    onnx_model.cpp
    onnx_model.h
    onnx_session.cpp
    onnx_session.h
    text_detector.cpp
    text_detector.h
    text_recognizer.cpp
    text_recognizer.h)
target_include_directories(tmw_ocr PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(tmw_ocr PUBLIC cxx_std_20)
target_link_libraries(tmw_ocr
    PUBLIC opencv_core opencv_imgproc onnxruntime::onnxruntime
    PRIVATE yaml-cpp::yaml-cpp nlohmann_json::nlohmann_json tmw_clipper tmw_options)
