#include "net/service_translators.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <utility>

#include "net/llm_prompt.h"
#include "net/ruby_notes.h"

namespace tmw::net {
namespace {

using core::TranslateError;
using core::TranslatorError;
using json = nlohmann::json;

std::string trimSlash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

// 來源語言：auto（或看不懂的）就不指定，讓服務自己判斷
std::string sourceCode(const std::string& language, bool upper) {
    if (language != "ja" && language != "en" && language != "ko") {
        return {};
    }
    if (!upper) {
        return language;
    }
    std::string out = language;
    std::ranges::transform(out, out.begin(),
                           [](char c) { return static_cast<char>(c - 'a' + 'A'); });
    return out;
}

// 字串陣列，任何一項不是字串就算格式不符
std::optional<std::vector<std::string>> stringsAt(const json& array, const char* field) {
    if (!array.is_array()) {
        return std::nullopt;
    }
    std::vector<std::string> out;
    for (const json& item : array) {
        const auto value = item.is_object() ? item.find(field) : item.end();
        if (!item.is_object() || value == item.end() || !value->is_string()) {
            return std::nullopt;
        }
        out.push_back(value->get<std::string>());
    }
    return out;
}

HttpRequest jsonPost(std::string url, const json& body, std::chrono::milliseconds timeout) {
    HttpRequest http;
    http.url = std::move(url);
    http.method = HttpMethod::Post;
    http.body = body.dump();
    http.timeout = timeout;
    http.headers.emplace_back("Content-Type", "application/json");
    return http;
}

}  // namespace

ServiceTranslator::ServiceTranslator(std::shared_ptr<IHttpClient> http, ServiceOptions options)
    : http_(std::move(http)), options_(std::move(options)) {}

core::TranslateError ServiceTranslator::classify(const HttpResponse& response) const {
    return classifyHttpFailure(response);
}

std::vector<std::string> ServiceTranslator::sendChunk(std::span<const std::string> texts,
                                                      const core::TranslateRequest& request,
                                                      std::stop_token cancel) {
    const HttpResponse response = http_->send(buildRequest(texts, request), cancel);
    if (cancel.stop_requested()) {
        throw TranslatorError(TranslateError::Cancelled, "翻譯被取消");
    }
    if (!response.ok()) {
        throw TranslatorError(classify(response), describeHttpFailure(response));
    }
    std::optional<std::vector<std::string>> parsed = parseReply(response.body);
    if (!parsed) {
        throw TranslatorError(TranslateError::BadResponse, "看不懂 " + id() + " 的回應");
    }
    if (parsed->size() != texts.size()) {
        throw TranslatorError(TranslateError::BadResponse, "譯文數量和原文不同");
    }
    return std::move(*parsed);
}

std::vector<std::string> ServiceTranslator::translate(std::span<const std::string> segments,
                                                      const core::TranslateRequest& request,
                                                      std::stop_token cancel) {
    // 看不懂 `{本文|讀音}`：標記還原成本文，特殊讀音另外翻、附在後面（net/ruby_notes）
    return translateWithRubyNotes(segments, [&](std::span<const std::string> all) {
        std::vector<std::string> out;
        out.reserve(all.size());
        // 超過這家一次的上限就分幾次送（一個畫面通常一次就夠了）
        for (std::size_t at = 0; at < all.size(); at += maxBatch()) {
            const std::size_t count = std::min(maxBatch(), all.size() - at);
            std::vector<std::string> part = sendChunk(all.subspan(at, count), request, cancel);
            out.insert(out.end(), part.begin(), part.end());
        }
        return out;
    });
}

// --- DeepL ------------------------------------------------------------------------------------

HttpRequest DeepLTranslator::buildRequest(std::span<const std::string> texts,
                                          const core::TranslateRequest& request) const {
    std::string base = options().endpoint;
    if (base.empty()) {
        // 免費方案的金鑰以 :fx 結尾，要用另一個網址（用錯會回 403）
        base = options().apiKey.ends_with(":fx") ? "https://api-free.deepl.com"
                                                 : "https://api.deepl.com";
    }
    json body;
    body["text"] = std::vector<std::string>(texts.begin(), texts.end());
    body["target_lang"] = "ZH-HANT";
    if (const std::string source = sourceCode(request.srcLang, /*upper=*/true); !source.empty()) {
        body["source_lang"] = source;
    }
    if (!request.context.empty()) {
        // 前文只當參考、不會被翻譯，也不計費（DeepL 的 context 參數）
        std::string context;
        for (const auto& [source, translation] : request.context) {
            context += (context.empty() ? "" : "\n") + source;
        }
        body["context"] = context;
    }
    HttpRequest http = jsonPost(trimSlash(base) + "/v2/translate", body, options().timeout);
    http.headers.emplace_back("Authorization", "DeepL-Auth-Key " + options().apiKey);
    return http;
}

std::optional<std::vector<std::string>> DeepLTranslator::parseReply(std::string_view body) const {
    const json parsed = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (!parsed.is_object() || !parsed.contains("translations")) {
        return std::nullopt;
    }
    return stringsAt(parsed["translations"], "text");
}

core::TranslateError DeepLTranslator::classify(const HttpResponse& response) const {
    if (response.connected() && response.status == 456) {
        return TranslateError::RateLimited;  // 這個月的額度用完了
    }
    return classifyHttpFailure(response);
}

// --- Microsoft Translator ---------------------------------------------------------------------

HttpRequest AzureTranslator::buildRequest(std::span<const std::string> texts,
                                          const core::TranslateRequest& request) const {
    const std::string base = options().endpoint.empty()
                                 ? std::string("https://api.cognitive.microsofttranslator.com")
                                 : options().endpoint;
    std::string url = trimSlash(base) + "/translate?api-version=3.0&to=zh-Hant";
    if (const std::string source = sourceCode(request.srcLang, /*upper=*/false); !source.empty()) {
        url += "&from=" + source;
    }
    json body = json::array();
    for (const std::string& text : texts) {
        body.push_back({{"Text", text}});
    }
    HttpRequest http = jsonPost(std::move(url), body, options().timeout);
    http.headers.emplace_back("Ocp-Apim-Subscription-Key", options().apiKey);
    if (!options().region.empty()) {
        http.headers.emplace_back("Ocp-Apim-Subscription-Region", options().region);
    }
    return http;
}

std::optional<std::vector<std::string>> AzureTranslator::parseReply(std::string_view body) const {
    const json parsed = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (!parsed.is_array()) {
        return std::nullopt;
    }
    std::vector<std::string> out;
    for (const json& item : parsed) {
        const auto translations = item.is_object() ? item.find("translations") : item.end();
        if (!item.is_object() || translations == item.end()) {
            return std::nullopt;
        }
        const std::optional<std::vector<std::string>> texts = stringsAt(*translations, "text");
        if (!texts || texts->empty()) {
            return std::nullopt;
        }
        out.push_back(texts->front());  // 只要求一種目標語言，所以只有一個
    }
    return out;
}

// --- Google Cloud Translation ------------------------------------------------------------------

HttpRequest GoogleCloudTranslator::buildRequest(std::span<const std::string> texts,
                                                const core::TranslateRequest& request) const {
    const std::string base = options().endpoint.empty()
                                 ? std::string("https://translation.googleapis.com")
                                 : options().endpoint;
    json body;
    body["q"] = std::vector<std::string>(texts.begin(), texts.end());
    body["target"] = "zh-TW";
    body["format"] = "text";  // 不然譯文裡的引號會變成 &quot; 這類 HTML 寫法
    if (const std::string source = sourceCode(request.srcLang, /*upper=*/false); !source.empty()) {
        body["source"] = source;
    }
    HttpRequest http =
        jsonPost(trimSlash(base) + "/language/translate/v2", body, options().timeout);
    // 金鑰放標頭不放網址：網址比較容易被記錄下來
    http.headers.emplace_back("X-Goog-Api-Key", options().apiKey);
    return http;
}

std::optional<std::vector<std::string>> GoogleCloudTranslator::parseReply(
    std::string_view body) const {
    const json parsed = json::parse(body, nullptr, /*allow_exceptions=*/false);
    const auto data = parsed.is_object() ? parsed.find("data") : parsed.end();
    if (!parsed.is_object() || data == parsed.end() || !data->is_object() ||
        !data->contains("translations")) {
        return std::nullopt;
    }
    return stringsAt((*data)["translations"], "translatedText");
}

}  // namespace tmw::net
