# tmw_overlay_preview：把「譯文蓋在原文上」的樣子存成 PNG（M3）。覆蓋層排除在螢幕擷取之外，
# 截圖看不到它，檢查效果用這個。翻譯引擎的組裝和主程式共用 src/app/translation_setup。
add_executable(tmw_overlay_preview
    main.cpp
    "${PROJECT_SOURCE_DIR}/src/app/translation_setup.cpp")
target_include_directories(tmw_overlay_preview PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tmw_overlay_preview PRIVATE tmw_net tmw_ocr tmw_platform tmw_core tmw_options)
tmw_copy_asan_runtime(tmw_overlay_preview)
tmw_copy_opencc(tmw_overlay_preview)
tmw_copy_onnxruntime(tmw_overlay_preview)
