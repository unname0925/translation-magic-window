#include "net/google_translator.h"

#include <algorithm>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <utility>

#include "core/translation_alignment.h"

namespace tmw::net {
namespace {

using core::TranslateError;
using core::TranslatorError;

// 端點認得的來源語言代碼。產品只支援這三種，其餘一律交給它自己判斷。
std::string sourceCode(const std::string& language) {
    if (language == "ja" || language == "en" || language == "ko") {
        return language;
    }
    return "auto";
}

std::string join(std::span<const std::string> segments) {
    std::string out;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i > 0) {
            out += '\n';
        }
        out += segments[i];
    }
    return out;
}

// 逐段重送時，一段的譯文不能含有換行，否則接回去會多出一段
std::string flatten(std::string text) {
    std::replace(text.begin(), text.end(), '\n', ' ');
    return text;
}

TranslateError classify(const HttpResponse& response) {
    if (!response.connected()) {
        return TranslateError::Network;
    }
    if (response.status == 429) {
        return TranslateError::RateLimited;
    }
    if (response.status >= 500) {
        return TranslateError::Network;
    }
    return TranslateError::BadResponse;
}

}  // namespace

std::optional<std::string> parseGoogleResponse(std::string_view body) {
    const nlohmann::json parsed = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (!parsed.is_array() || parsed.empty()) {
        return std::nullopt;
    }
    const nlohmann::json& pieces = parsed[0];
    if (pieces.is_null()) {
        return std::string();  // 原文全是空白時端點會回 null
    }
    if (!pieces.is_array()) {
        return std::nullopt;
    }
    std::string out;
    for (const nlohmann::json& piece : pieces) {
        if (!piece.is_array() || piece.empty()) {
            continue;
        }
        if (piece[0].is_string()) {
            out += piece[0].get<std::string>();
        }
    }
    return out;
}

GoogleTranslator::GoogleTranslator(std::shared_ptr<IHttpClient> http, const core::IClock& clock,
                                   Options options, Sleeper sleeper)
    : http_(std::move(http)),
      clock_(clock),
      options_(std::move(options)),
      sleeper_(std::move(sleeper)),
      limiter_(options_.minimumInterval) {}

std::string GoogleTranslator::requestOnce(std::string_view text,
                                          const core::TranslateRequest& request,
                                          std::stop_token cancel) {
    const Duration wait = limiter_.waitFor(clock_.now());
    if (wait > Duration::zero() && sleeper_) {
        sleeper_(wait, cancel);
    }
    if (cancel.stop_requested()) {
        throw TranslatorError(TranslateError::Cancelled, "翻譯被取消");
    }

    HttpRequest http;
    http.url = buildUrl(options_.endpoint, {{"client", "gtx"},
                                            {"sl", sourceCode(request.srcLang)},
                                            {"tl", request.dstLang},
                                            {"dt", "t"},
                                            {"q", std::string(text)}});
    http.timeout = options_.timeout;
    // 沒有 User-Agent 時端點會回 403
    http.headers.emplace_back("User-Agent", "Mozilla/5.0");

    limiter_.record(clock_.now());
    const HttpResponse response = http_->send(http, cancel);
    if (cancel.stop_requested()) {
        throw TranslatorError(TranslateError::Cancelled, "翻譯被取消");
    }
    if (!response.ok()) {
        throw TranslatorError(classify(response), response.connected()
                                                      ? "HTTP " + std::to_string(response.status)
                                                      : response.error);
    }
    std::optional<std::string> translated = parseGoogleResponse(response.body);
    if (!translated) {
        throw TranslatorError(TranslateError::BadResponse, "看不懂端點的回應");
    }
    return std::move(*translated);
}

std::vector<std::string> GoogleTranslator::translate(std::span<const std::string> segments,
                                                     const core::TranslateRequest& request,
                                                     std::stop_token cancel) {
    const core::BatchTranslate batch =
        [&](std::span<const std::string> batchSegments) -> std::vector<std::string> {
        const std::string reply = requestOnce(join(batchSegments), request, cancel);
        if (batchSegments.size() == 1) {
            return {flatten(reply)};
        }
        return core::splitLines(reply);
    };
    // 這個端點沒有隨機性，整批重送只會多等一秒，所以對不上就直接逐段
    return core::translateAligned(segments, batch, core::AlignOptions{.batchAttempts = 1});
}

}  // namespace tmw::net
