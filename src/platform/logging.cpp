#include "platform/logging.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

#include "platform/text_encoding.h"

namespace tmw::platform {
namespace {

constexpr const char* kLoggerName = "tmw";
constexpr const char* kFileName = "translation-magic-window.log";
constexpr const char* kRedacted = "（略）";

std::atomic<bool> g_verboseDiagnostics{false};

spdlog::level::level_enum toSpdlog(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:
            return spdlog::level::trace;
        case LogLevel::Debug:
            return spdlog::level::debug;
        case LogLevel::Info:
            return spdlog::level::info;
        case LogLevel::Warn:
            return spdlog::level::warn;
        case LogLevel::Error:
            return spdlog::level::err;
    }
    return spdlog::level::info;
}

std::shared_ptr<spdlog::logger> logger() {
    return spdlog::get(kLoggerName);
}

}  // namespace

void initializeLogging(const LogOptions& options) {
    std::filesystem::create_directories(options.directory);
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        (options.directory / kFileName).string(), options.maxFileBytes, options.maxFiles));
    if (options.alsoToStderr) {
        sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
    }

    spdlog::drop(kLoggerName);
    auto created = std::make_shared<spdlog::logger>(kLoggerName, sinks.begin(), sinks.end());
    created->set_level(toSpdlog(options.level));
    // 程式當掉或被強制結束時，已經記錄的內容不可以留在緩衝裡：info 以上每筆都寫出，
    // 量大的 debug、trace 則每秒寫一次。
    created->flush_on(spdlog::level::info);
    spdlog::flush_every(std::chrono::seconds(1));
    // 時間、等級、訊息。透鏡編號和流水號由 log() 加在訊息前面。
    created->set_pattern("%Y-%m-%d %H:%M:%S.%e [%^%l%$] %v");
    spdlog::register_logger(created);
    g_verboseDiagnostics.store(options.verboseDiagnostics, std::memory_order_relaxed);
}

void shutdownLogging() {
    if (const auto current = logger(); current != nullptr) {
        current->flush();
    }
    spdlog::drop(kLoggerName);
    g_verboseDiagnostics.store(false, std::memory_order_relaxed);
}

void setVerboseDiagnostics(bool verbose) {
    g_verboseDiagnostics.store(verbose, std::memory_order_relaxed);
}

bool verboseDiagnostics() {
    return g_verboseDiagnostics.load(std::memory_order_relaxed);
}

std::string pathToUtf8(const std::filesystem::path& path) {
    return wideToUtf8(path.wstring());
}

std::string sensitive(std::string_view text) {
    if (!verboseDiagnostics()) {
        return kRedacted;
    }
    return std::string(text);
}

void log(LogLevel level, std::string_view message) {
    if (const auto current = logger(); current != nullptr) {
        current->log(toSpdlog(level), "{}", message);
    }
}

void log(LogLevel level, const LogContext& context, std::string_view message) {
    if (const auto current = logger(); current != nullptr) {
        current->log(toSpdlog(level), "[透鏡 {} #{}] {}", context.lens, context.sequence, message);
    }
}

}  // namespace tmw::platform
