// 自訂 HTTP 範本引擎（M2-07，design.md 4.5 的引擎表）：
// 使用者自己填網址、標頭、請求內容的範本，以及從回應取出譯文的 JSON 路徑，
// 就能接上程式沒有內建的翻譯服務（LibreTranslate、Papago、公司內部的服務…）。
//
// 範本裡的佔位符：
//   {{text}}    原文，已經做好 JSON 字串的跳脫（直接放在 "…" 裡面）；在網址裡會做網址編碼
//   {{source}}  來源語言：ja、en、ko，或 auto
//   {{target}}  目標語言：zh-TW
//   {{key}}     金鑰（加密存放，送出時才填進去）
// 一段文字送一次。請求內容是空的就用 GET。
// JSON 路徑：「translations[0].text」「data.translatedText」「[0].translations[0].text」。
#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "core/translator.h"
#include "net/http_client.h"

namespace tmw::net {

struct CustomHttpOptions {
    std::string id = "custom-http";
    std::string url;
    std::string headers;       // 一行一個「名稱: 值」
    std::string bodyTemplate;  // 空的就用 GET
    std::string responsePath;  // 譯文在回應 JSON 裡的位置
    std::string apiKey;
    std::chrono::milliseconds timeout{30000};
};

class CustomHttpTranslator final : public core::ITranslator {
public:
    CustomHttpTranslator(std::shared_ptr<IHttpClient> http, CustomHttpOptions options);

    std::string id() const override { return options_.id; }
    bool supportsBatch() const override { return false; }

    std::vector<std::string> translate(std::span<const std::string> segments,
                                       const core::TranslateRequest& request,
                                       std::stop_token cancel) override;

    // 送出去的請求（測試用，不連線）
    HttpRequest buildRequest(const std::string& text, const core::TranslateRequest& request) const;

private:
    std::string translateOne(const std::string& text, const core::TranslateRequest& request,
                             std::stop_token cancel);

    std::shared_ptr<IHttpClient> http_;
    CustomHttpOptions options_;
};

// 依 JSON 路徑取出字串。路徑錯了、或那個位置不是字串時回傳 nullopt。
std::optional<std::string> extractJsonPath(std::string_view body, std::string_view path);

// 範本有沒有問題（給設定視窗在存檔前檢查）。沒問題回傳空字串。
std::string customHttpProblem(const CustomHttpOptions& options);

}  // namespace tmw::net
