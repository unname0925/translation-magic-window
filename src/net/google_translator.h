// Google 網頁翻譯的非官方端點（見 docs/design.md 4.5）。
//
// 沒有金鑰時的預設引擎。M0-12 的評測中它明顯比 LLM 差（尤其是沒有上下文的短字串），
// 但不用申請、不用錢，適合當引擎鏈的最後一道。
//
// 一次把整批段落用換行接起來送出，回來再切開；數量對不上就逐段重送（設計文件 4.5 步驟 4）。
// 為了降低被封鎖的風險，每秒最多送一個請求。
#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "core/clock.h"
#include "core/translator.h"
#include "net/http_client.h"
#include "net/rate_limiter.h"

namespace tmw::net {

class GoogleTranslator final : public core::ITranslator {
public:
    struct Options {
        std::string endpoint = "https://translate.googleapis.com/translate_a/single";
        Duration minimumInterval = std::chrono::seconds(1);
        std::chrono::milliseconds timeout{15000};
    };

    GoogleTranslator(std::shared_ptr<IHttpClient> http, const core::IClock& clock,
                     Options options = {}, Sleeper sleeper = defaultSleeper());

    std::string id() const override { return "google"; }
    bool supportsBatch() const override { return true; }

    std::vector<std::string> translate(std::span<const std::string> segments,
                                       const core::TranslateRequest& request,
                                       std::stop_token cancel) override;

private:
    std::string requestOnce(std::string_view text, const core::TranslateRequest& request,
                            std::stop_token cancel);

    std::shared_ptr<IHttpClient> http_;
    const core::IClock& clock_;
    Options options_;
    Sleeper sleeper_;
    RateLimiter limiter_;
};

// 從端點的回應中取出譯文。回應是巢狀陣列，第一個元素是一段一段的譯文，
// 每一段的第一個元素才是文字：[[["你好","こんにちは",...],...],...]
// 格式不符時回傳 nullopt。
std::optional<std::string> parseGoogleResponse(std::string_view body);

}  // namespace tmw::net
