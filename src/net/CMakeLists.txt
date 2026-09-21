# net：翻譯引擎和 HTTP（見 docs/design.md 3.2）。
# core 不碰網路，實際會連線的東西都住在這裡。
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(tmw_net STATIC
    cpr_http_client.cpp
    cpr_http_client.h
    google_translator.cpp
    google_translator.h
    http_client.cpp
    http_client.h
    openai_translator.cpp
    openai_translator.h
    rate_limiter.cpp
    rate_limiter.h
    sse.cpp
    sse.h)
target_include_directories(tmw_net PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(tmw_net PUBLIC cxx_std_20)
target_link_libraries(tmw_net
    PUBLIC tmw_core
    PRIVATE cpr::cpr nlohmann_json::nlohmann_json tmw_options)
