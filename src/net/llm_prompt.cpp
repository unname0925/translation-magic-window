#include "net/llm_prompt.h"

#include <nlohmann/json.hpp>
#include <string_view>
#include <utility>
#include <vector>

namespace tmw::net {
namespace {

// M0-12 實測過的提示詞（盲評 4.13～4.46 分），原樣沿用
constexpr std::string_view kSystemPrompt =
    "你是翻譯引擎。把 segments 中的每個字串翻譯成台灣繁體中文，保留語氣和角色口吻。"
    "`{本文|讀音}` 是日文漫畫中作者刻意標的特殊讀音（左邊是字面，右邊是實際的意思），"
    "譯文要在對應的位置保留同樣的標記。"
    "只輸出和 segments 等長的 JSON 字串陣列，不要加任何說明。";

constexpr std::string_view kPlainPrompt =
    "你是翻譯引擎。把使用者傳來的文字翻譯成台灣繁體中文，保留語氣和角色口吻。"
    "`{本文|讀音}` 標記要保留。只輸出譯文，不要加引號或任何說明。";

// 有專有名詞時才加在後面（M2-09）。沒有詞條的請求和 M0-12 評測過的提示詞一字不差。
constexpr std::string_view kGlossaryRule =
    "glossary 是專有名詞表（原文 → 譯文），這些詞一律照表翻譯。";

}  // namespace

std::string llmSystemPrompt(const core::TranslateRequest& request, bool asJsonArray) {
    std::string prompt(asJsonArray ? kSystemPrompt : kPlainPrompt);
    if (request.glossary.empty()) {
        return prompt;
    }
    if (asJsonArray) {
        prompt += kGlossaryRule;  // 詞表本身在使用者訊息的 JSON 裡
        return prompt;
    }
    // 只送一段時使用者訊息是純文字，詞表只能放在這裡
    prompt += "專有名詞照這個表翻譯：";
    bool first = true;
    for (const auto& [source, target] : request.glossary) {
        prompt += first ? "" : "、";
        prompt += source + " → " + target;
        first = false;
    }
    prompt += "。";
    return prompt;
}

std::string llmUserMessage(const core::TranslateRequest& request,
                           std::span<const std::string> segments, bool asJsonArray) {
    if (!asJsonArray) {
        return segments.empty() ? std::string() : segments[0];
    }
    nlohmann::json user;
    user["source_lang"] = request.srcLang;
    if (!request.context.empty()) {
        nlohmann::json context = nlohmann::json::array();
        for (const auto& [source, translation] : request.context) {
            context.push_back({source, translation});
        }
        user["context"] = std::move(context);
    }
    if (!request.glossary.empty()) {
        user["glossary"] = request.glossary;
    }
    user["segments"] = std::vector<std::string>(segments.begin(), segments.end());
    return user.dump();
}

core::TranslateError classifyHttpFailure(const HttpResponse& response) {
    if (!response.connected()) {
        return core::TranslateError::Network;
    }
    if (response.status == 429) {
        return core::TranslateError::RateLimited;
    }
    if (response.status >= 500) {
        return core::TranslateError::Network;  // 含 Anthropic 的 529（服務過載）
    }
    // 其餘的 4xx（金鑰錯誤、模型名稱錯誤、被擋下來）重送同樣的內容也不會變好
    return core::TranslateError::Rejected;
}

std::string describeHttpFailure(const HttpResponse& response) {
    if (!response.connected()) {
        return response.error;
    }
    std::string out = "HTTP " + std::to_string(response.status);
    const nlohmann::json parsed =
        nlohmann::json::parse(response.body, nullptr, /*allow_exceptions=*/false);
    if (!parsed.is_object()) {
        return out;
    }
    // OpenAI、Anthropic、Microsoft、Google Cloud 放在 error.message；DeepL 放在最上層的 message
    const auto error = parsed.find("error");
    const nlohmann::json* holder = error != parsed.end() && error->is_object() ? &*error : &parsed;
    const auto message = holder->find("message");
    if (message != holder->end() && message->is_string()) {
        out += "：" + message->get<std::string>();
    }
    return out;
}

}  // namespace tmw::net
