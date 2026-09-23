#include "core/debug_report.h"

#include <nlohmann/json.hpp>
#include <utility>

#include "core/language.h"

namespace tmw::core {
namespace {

using nlohmann::json;

constexpr const char* kRemoved = "（已移除）";

json toJson(const RectI& rect) {
    return json{
        {"left", rect.left}, {"top", rect.top}, {"right", rect.right}, {"bottom", rect.bottom}};
}

const char* orientationName(Orientation orientation) {
    return orientation == Orientation::Vertical ? "vertical" : "horizontal";
}

json toJson(const std::vector<RubyAnnotation>& ruby) {
    json array = json::array();
    for (const RubyAnnotation& annotation : ruby) {
        array.push_back(json{{"start", annotation.start},
                             {"length", annotation.length},
                             {"reading", annotation.reading}});
    }
    return array;
}

json toJson(const OcrLine& line) {
    return json{{"text", line.text},
                {"rect", toJson(line.rect)},
                {"score", line.score},
                {"orientation", orientationName(line.orientation)},
                {"ruby", toJson(line.ruby)}};
}

json toJson(const TextBlock& block) {
    json lines = json::array();
    for (const OcrLine& line : block.lines) {
        lines.push_back(toJson(line));
    }
    return json{{"text", block.text},
                {"rect", toJson(block.rect)},
                {"score", block.score},
                {"orientation", orientationName(block.orientation)},
                {"language", languageCode(block.language)},
                {"ruby", toJson(block.ruby)},
                {"lines", std::move(lines)}};
}

json toJson(const PipelineResult& result) {
    json groups = json::array();
    for (const TranslatedBlock& group : result.groups) {
        groups.push_back(json{{"source", toJson(group.block)}, {"translation", group.translation}});
    }
    return json{{"lens", result.lens},
                {"generation", result.generation},
                {"region", toJson(result.region)},
                {"language", languageCode(result.language)},
                {"unchanged", result.unchanged},
                {"error", result.error},
                {"timings", json{{"ocrMs", result.timings.ocrMs},
                                 {"layoutMs", result.timings.layoutMs},
                                 {"translationMs", result.timings.translationMs},
                                 {"totalMs", result.timings.totalMs()}}},
                {"groups", std::move(groups)}};
}

json toJson(const Settings& settings) {
    json engines = json::array();
    for (const EngineSettings& engine : settings.engines) {
        engines.push_back(json{{"id", engine.id},
                               {"endpoint", engine.endpoint},
                               {"model", engine.model},
                               {"apiKey", engine.encryptedApiKey}});
    }
    return json{{"verboseDiagnostics", settings.verboseDiagnostics},
                {"engines", std::move(engines)}};
}

}  // namespace

Settings withoutSecrets(Settings settings) {
    for (EngineSettings& engine : settings.engines) {
        // 有沒有設定過金鑰是查問題時要知道的，金鑰本身不是
        engine.encryptedApiKey = engine.encryptedApiKey.empty() ? std::string() : kRemoved;
    }
    return settings;
}

std::string buildDebugReport(const DebugReportInput& input) {
    json report{{"app", json{{"name", "Translation Magic Window"},
                             {"version", input.appVersion},
                             {"time", input.time}}},
                {"ocrDevice", input.ocrDevice},
                {"engineStatus", input.engineStatus},
                {"settings", toJson(withoutSecrets(input.settings))}};

    if (input.lastResult.has_value()) {
        report["lastResult"] = toJson(*input.lastResult);
    } else {
        report["lastResult"] = nullptr;
    }

    json lines = json::array();
    for (const OcrLine& line : input.lastLines) {
        lines.push_back(toJson(line));
    }
    report["ocrLines"] = std::move(lines);
    report["perf"] = input.perfReport;

    return report.dump(2) + "\n";
}

}  // namespace tmw::core
