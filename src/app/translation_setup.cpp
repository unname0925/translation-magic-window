#include "app/translation_setup.h"

#include <exception>
#include <optional>
#include <utility>

#include "core/opencc_converter.h"
#include "core/text_converter.h"
#include "core/translator_chain.h"
#include "net/anthropic_translator.h"
#include "net/cpr_http_client.h"
#include "net/custom_http_translator.h"
#include "net/google_translator.h"
#include "net/openai_translator.h"
#include "net/service_translators.h"
#include "platform/secret.h"

namespace tmw::app {
namespace {

// 本機的 Ollama（預設的連接埠 11434）。網址可能寫 localhost 或 127.0.0.1
bool isLocalOllama(std::string_view url) {
    return (url.find("://127.0.0.1:11434") != std::string_view::npos ||
            url.find("://localhost:11434") != std::string_view::npos);
}

std::shared_ptr<core::ITranslator> makeEngine(const core::EngineSettings& engine,
                                              const core::IClock& clock,
                                              std::vector<std::string>& problems) {
    // 每個引擎各自持有一個用戶端，連線才能重複使用（M0-12）
    auto http = std::make_shared<net::CprHttpClient>();

    if (engine.id == "google") {
        net::GoogleTranslator::Options options;
        if (!engine.endpoint.empty()) {
            options.endpoint = engine.endpoint;
        }
        return std::make_shared<net::GoogleTranslator>(std::move(http), clock, std::move(options));
    }
    if (engine.id == "openai-compatible") {
        net::OpenAiTranslator::Options options;
        options.id = engine.id;
        if (!engine.endpoint.empty()) {
            options.baseUrl = engine.endpoint;
        }
        if (!engine.model.empty()) {
            options.model = engine.model;
        }
        if (isLocalOllama(options.baseUrl)) {
            options.ollamaKeepAlive = "30m";  // 閒置後第一次要等約 10 秒重新載入模型
        }
        if (!engine.encryptedApiKey.empty()) {
            const std::optional<std::string> key = platform::decryptSecret(engine.encryptedApiKey);
            if (!key) {
                problems.push_back("引擎 " + engine.id +
                                   " 的金鑰解不開（可能是換了使用者或電腦），改成不帶金鑰");
            } else {
                options.apiKey = *key;
            }
        }
        return std::make_shared<net::OpenAiTranslator>(std::move(http), std::move(options));
    }
    if (engine.id == "anthropic") {
        net::AnthropicTranslator::Options options;
        if (!engine.endpoint.empty()) {
            options.baseUrl = engine.endpoint;
        }
        if (!engine.model.empty()) {
            options.model = engine.model;
        }
        const std::optional<std::string> key =
            engine.encryptedApiKey.empty() ? std::nullopt
                                           : platform::decryptSecret(engine.encryptedApiKey);
        if (!key) {
            // Claude 的 API 一定要金鑰：沒有就不加進引擎鏈，免得每次都先等它失敗
            problems.push_back("引擎 anthropic 沒有可用的金鑰，略過");
            return nullptr;
        }
        options.apiKey = *key;
        return std::make_shared<net::AnthropicTranslator>(std::move(http), std::move(options));
    }
    if (engine.id == "deepl" || engine.id == "azure" || engine.id == "google-cloud") {
        const std::optional<std::string> key =
            engine.encryptedApiKey.empty() ? std::nullopt
                                           : platform::decryptSecret(engine.encryptedApiKey);
        if (!key) {
            problems.push_back("引擎 " + engine.id + " 沒有可用的金鑰，略過");
            return nullptr;
        }
        net::ServiceOptions options;
        options.id = engine.id;
        options.apiKey = *key;
        options.endpoint = engine.endpoint;
        options.region = engine.region;
        if (engine.id == "deepl") {
            return std::make_shared<net::DeepLTranslator>(std::move(http), std::move(options));
        }
        if (engine.id == "azure") {
            return std::make_shared<net::AzureTranslator>(std::move(http), std::move(options));
        }
        return std::make_shared<net::GoogleCloudTranslator>(std::move(http), std::move(options));
    }
    if (engine.id == "custom-http") {
        net::CustomHttpOptions options;
        options.url = engine.endpoint;
        options.headers = engine.headers;
        options.bodyTemplate = engine.bodyTemplate;
        options.responsePath = engine.responsePath;
        if (!engine.encryptedApiKey.empty()) {
            const std::optional<std::string> key = platform::decryptSecret(engine.encryptedApiKey);
            if (!key) {
                problems.push_back("引擎 custom-http 的金鑰解不開，改成不帶金鑰");
            } else {
                options.apiKey = *key;
            }
        }
        if (const std::string problem = net::customHttpProblem(options); !problem.empty()) {
            problems.push_back("自訂 HTTP 範本不能用（" + problem + "），略過");
            return nullptr;
        }
        return std::make_shared<net::CustomHttpTranslator>(std::move(http), std::move(options));
    }
    problems.push_back("不認得的翻譯引擎：" + engine.id);
    return nullptr;
}

}  // namespace

TranslationSetup makeTranslationService(const core::Settings& settings, const core::IClock& clock,
                                        const std::filesystem::path& openccConfig) {
    TranslationSetup setup;

    std::vector<core::EngineSettings> wanted = settings.engines;
    if (wanted.empty()) {
        // 沒設定就用不需要金鑰的 Google，至少能翻
        wanted.push_back(core::EngineSettings{"google", "", "", ""});
    }

    std::vector<std::shared_ptr<core::ITranslator>> engines;
    for (const core::EngineSettings& engine : wanted) {
        std::shared_ptr<core::ITranslator> made = makeEngine(engine, clock, setup.problems);
        if (made != nullptr) {
            setup.engineIds.push_back(made->id());
            engines.push_back(std::move(made));
        }
    }

    std::shared_ptr<const core::ITextConverter> converter;
    try {
        converter = std::make_shared<core::OpenccConverter>(openccConfig);
    } catch (const std::exception& error) {
        setup.problems.push_back(std::string("簡轉繁不能用，譯文照原樣顯示：") + error.what());
        converter = std::make_shared<core::NullTextConverter>();
    }

    auto chain = std::make_shared<core::TranslatorChain>(std::move(engines), clock);
    setup.service =
        std::make_shared<core::TranslationService>(std::move(chain), std::move(converter));
    return setup;
}

}  // namespace tmw::app
