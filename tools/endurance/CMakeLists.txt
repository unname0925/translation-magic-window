# tmw_endurance：耐久測試（M1-16、execution-plan.md 5.7）。會啟動主程式，需要桌面環境。
add_executable(tmw_endurance main.cpp)
target_link_libraries(tmw_endurance PRIVATE tmw_options user32 gdi32 psapi)
add_dependencies(tmw_endurance tmw_app)
