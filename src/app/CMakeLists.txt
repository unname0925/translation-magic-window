add_executable(tmw_app WIN32
    app.manifest
    app_controller.cpp
    app_controller.h
    app_identity.h
    main.cpp
    translation_setup.cpp
    translation_setup.h)
set_target_properties(tmw_app PROPERTIES OUTPUT_NAME "TranslationMagicWindow")
target_include_directories(tmw_app PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tmw_app PRIVATE tmw_ui tmw_net tmw_ocr tmw_platform tmw_core tmw_options)
tmw_copy_asan_runtime(tmw_app)
tmw_copy_opencc(tmw_app)
tmw_copy_onnxruntime(tmw_app)
tmw_copy_qt(tmw_app)
