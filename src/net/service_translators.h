// 付費翻譯服務的引擎（M2-07，design.md 4.5）：DeepL、Microsoft Translator、Google Cloud
// Translation。
//
// 三家都是「一次送一個陣列、回傳等長的陣列」，比 Google 網頁翻譯的非官方端點穩定，
// 也不像 LLM 會自己改格式。都看不懂 `{本文|讀音}`，用 net/ruby_notes 的做法處理特殊讀音。
// 專有名詞表不支援（各家的詞彙表要先用 API 建好，不能每次請求帶）。
//
// 格式依各家的官方文件；寫的時候沒有金鑰，沒有對真的服務實測過（見 execution-plan.md M2-07）。
#pragma once

#include <chrono>
#include <cstddef>
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

struct ServiceOptions {
    std::string id;
    std::string apiKey;
    // 留空用各家的預設網址。Microsoft 的「區域」另外放在 region。
    std::string endpoint;
    // Microsoft 區域型資源的區域（例如 eastasia），全域資源留空
    std::string region;
    std::chrono::milliseconds timeout{30000};
};

// 一家服務：怎麼組請求、怎麼讀回應、一次最多幾段
class ServiceTranslator : public core::ITranslator {
public:
    ServiceTranslator(std::shared_ptr<IHttpClient> http, ServiceOptions options);

    std::string id() const override { return options_.id; }
    bool supportsBatch() const override { return true; }

    std::vector<std::string> translate(std::span<const std::string> segments,
                                       const core::TranslateRequest& request,
                                       std::stop_token cancel) override;

    // 送出去的請求（測試用，不連線）。texts 已經去掉 ルビ 標記。
    virtual HttpRequest buildRequest(std::span<const std::string> texts,
                                     const core::TranslateRequest& request) const = 0;
    // 從回應取出譯文。格式不符時回傳 nullopt。
    virtual std::optional<std::vector<std::string>> parseReply(std::string_view body) const = 0;

protected:
    virtual std::size_t maxBatch() const = 0;
    // 這家把某個狀態碼當成其他意思時覆寫（DeepL 的 456 是額度用完）
    virtual core::TranslateError classify(const HttpResponse& response) const;

    const ServiceOptions& options() const { return options_; }

private:
    std::vector<std::string> sendChunk(std::span<const std::string> texts,
                                       const core::TranslateRequest& request,
                                       std::stop_token cancel);

    std::shared_ptr<IHttpClient> http_;
    ServiceOptions options_;
};

// DeepL：https://api.deepl.com/v2/translate（金鑰以 :fx 結尾的免費方案用 api-free.deepl.com）
class DeepLTranslator final : public ServiceTranslator {
public:
    using ServiceTranslator::ServiceTranslator;
    HttpRequest buildRequest(std::span<const std::string> texts,
                             const core::TranslateRequest& request) const override;
    std::optional<std::vector<std::string>> parseReply(std::string_view body) const override;

protected:
    std::size_t maxBatch() const override { return 50; }
    core::TranslateError classify(const HttpResponse& response) const override;
};

// Microsoft Translator：https://api.cognitive.microsofttranslator.com/translate?api-version=3.0
class AzureTranslator final : public ServiceTranslator {
public:
    using ServiceTranslator::ServiceTranslator;
    HttpRequest buildRequest(std::span<const std::string> texts,
                             const core::TranslateRequest& request) const override;
    std::optional<std::vector<std::string>> parseReply(std::string_view body) const override;

protected:
    std::size_t maxBatch() const override { return 100; }
};

// Google Cloud Translation（Basic，v2）：https://translation.googleapis.com/language/translate/v2
class GoogleCloudTranslator final : public ServiceTranslator {
public:
    using ServiceTranslator::ServiceTranslator;
    HttpRequest buildRequest(std::span<const std::string> texts,
                             const core::TranslateRequest& request) const override;
    std::optional<std::vector<std::string>> parseReply(std::string_view body) const override;

protected:
    std::size_t maxBatch() const override { return 128; }
};

}  // namespace tmw::net
