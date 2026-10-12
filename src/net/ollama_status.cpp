#include "net/ollama_status.h"

#include <chrono>
#include <nlohmann/json.hpp>
#include <string>

namespace tmw::net {
namespace {

bool sameModel(std::string_view loaded, std::string_view wanted) {
    if (loaded == wanted) {
        return true;
    }
    // 沒寫標籤的名字就是 :latest
    const auto withLatest = [](std::string_view name) {
        return name.find(':') == std::string_view::npos ? std::string(name) + ":latest"
                                                        : std::string(name);
    };
    return withLatest(loaded) == withLatest(wanted);
}

}  // namespace

std::optional<OllamaPlacement> parseOllamaPlacement(std::string_view psJson,
                                                    std::string_view model) {
    const nlohmann::json reply = nlohmann::json::parse(psJson, nullptr, false);
    if (!reply.is_object() || !reply.contains("models") || !reply["models"].is_array()) {
        return std::nullopt;
    }
    for (const nlohmann::json& entry : reply["models"]) {
        const std::string name = entry.value("name", entry.value("model", std::string()));
        if (!sameModel(name, model) || !entry.contains("size") || !entry["size"].is_number()) {
            continue;
        }
        OllamaPlacement placement;
        placement.size = entry["size"].get<std::uint64_t>();
        placement.onGpu = entry.contains("size_vram") && entry["size_vram"].is_number()
                              ? entry["size_vram"].get<std::uint64_t>()
                              : 0;
        return placement;
    }
    return std::nullopt;
}

std::optional<OllamaPlacement> ollamaPlacement(IHttpClient& http, std::string_view model,
                                               std::stop_token cancel) {
    HttpRequest request;
    request.url = std::string(kLocalOllama) + "/api/ps";
    request.timeout = std::chrono::seconds(3);
    const HttpResponse response = http.send(request, cancel);
    if (!response.ok()) {
        return std::nullopt;
    }
    return parseOllamaPlacement(response.body, model);
}

bool setOllamaKeepAlive(IHttpClient& http, std::string_view model, std::string_view keepAlive,
                        std::stop_token cancel) {
    HttpRequest request;
    request.url = std::string(kLocalOllama) + "/api/generate";
    request.method = HttpMethod::Post;
    request.headers = {{"Content-Type", "application/json"}};
    nlohmann::json body{{"model", std::string(model)}};
    if (keepAlive == "0") {
        body["keep_alive"] = 0;  // 數字 0 是馬上卸載；字串 "0" 沒有單位，不是合法的時間長度
    } else {
        body["keep_alive"] = std::string(keepAlive);
    }
    request.body = body.dump();
    request.timeout = std::chrono::minutes(2);  // 從硬碟載入 5 GB 的模型
    return http.send(request, cancel).ok();
}

OllamaPlacementCheck checkOllamaPlacement(IHttpClient& http, std::string_view model,
                                          std::string_view keepAlive, bool mayReload,
                                          std::stop_token cancel) {
    OllamaPlacementCheck check;
    check.before = ollamaPlacement(http, model, cancel);
    if (!check.before.has_value()) {
        return check;  // 沒載入
    }
    if (check.before->fullyOnGpu()) {
        check.action = OllamaPlacementAction::Full;
        return check;
    }
    check.action = OllamaPlacementAction::StillPartial;
    if (!mayReload || !setOllamaKeepAlive(http, model, "0", cancel) ||
        !setOllamaKeepAlive(http, model, keepAlive, cancel)) {
        return check;
    }
    check.after = ollamaPlacement(http, model, cancel);
    if (check.after.has_value() && check.after->fullyOnGpu()) {
        check.action = OllamaPlacementAction::Reloaded;
    }
    return check;
}

}  // namespace tmw::net
