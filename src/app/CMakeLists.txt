add_executable(tmw_app WIN32
    app.manifest
    app_controller.cpp
    app_controller.h
    app_identity.h
    main.cpp)
set_target_properties(tmw_app PROPERTIES OUTPUT_NAME "TranslationMagicWindow")
target_include_directories(tmw_app PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tmw_app PRIVATE tmw_platform tmw_core tmw_options)
tmw_copy_asan_runtime(tmw_app)
