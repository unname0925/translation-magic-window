// 手動實測翻譯引擎：把命令列上的每一段文字翻成繁體中文並印出來。
//
//   tmw_translate_cli --engine google --lang ja こんにちは セーブ
//
// 用途是確認引擎真的能連上端點（單元測試用的是錄好的回應），以及看一眼譯文的品質。
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include "core/clock.h"
#include "core/glossary.h"
#include "core/translator.h"
#include "net/anthropic_translator.h"
#include "net/cpr_http_client.h"
#include "net/google_translator.h"
#include "net/openai_translator.h"
#include "platform/text_encoding.h"

namespace {

void print(const std::string& utf8) {
    std::fputs(utf8.c_str(), stdout);
    std::fputc('\n', stdout);
}

int usage() {
    print("用法：tmw_translate_cli [--engine google|openai|anthropic] [--lang ja|en|ko|auto]");
    print("      [--base-url URL] [--model 名稱] [--key-env 環境變數] [--no-stream]");
    print("      [--glossary 專有名詞表.txt] [--print-request] 文字...");
    print("--print-request：只印出 anthropic 引擎要送出的請求內容，不連線（錄測試資料用）。");
    print("金鑰只能用環境變數傳入（--key-env），不要直接打在命令列上。");
    return 2;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    // 主控台用 UTF-8 才看得到中文和日文
    SetConsoleOutputCP(CP_UTF8);

    std::string engine = "google";
    std::string language = "auto";
    tmw::net::OpenAiTranslator::Options llm;
    llm.baseUrl = "http://127.0.0.1:11434/v1";
    llm.model = "hy-mt2";
    std::string keyVariable;
    // --base-url、--model 有給才覆寫：anthropic 的預設值和 openai 的不同
    std::string baseUrl;
    std::string model;
    bool printRequest = false;
    tmw::core::Glossary glossary;
    std::vector<std::string> segments;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = tmw::platform::wideToUtf8(argv[i]);
        if (argument == "--engine" && i + 1 < argc) {
            engine = tmw::platform::wideToUtf8(argv[++i]);
        } else if (argument == "--lang" && i + 1 < argc) {
            language = tmw::platform::wideToUtf8(argv[++i]);
        } else if (argument == "--base-url" && i + 1 < argc) {
            baseUrl = tmw::platform::wideToUtf8(argv[++i]);
        } else if (argument == "--model" && i + 1 < argc) {
            model = tmw::platform::wideToUtf8(argv[++i]);
        } else if (argument == "--print-request") {
            printRequest = true;
        } else if (argument == "--key-env" && i + 1 < argc) {
            keyVariable = tmw::platform::wideToUtf8(argv[++i]);
        } else if (argument == "--glossary" && i + 1 < argc) {
            // 和主程式的 glossary.txt 同樣的格式（core/glossary.h）
            std::ifstream file(argv[++i], std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
            const tmw::core::GlossaryLoad load = tmw::core::parseGlossary(text);
            for (const std::string& problem : load.problems) {
                print("專有名詞表" + problem);
            }
            glossary = load.entries;
        } else if (argument == "--no-stream") {
            llm.stream = false;
        } else if (argument == "--help" || argument == "-h") {
            return usage();
        } else {
            segments.push_back(argument);
        }
    }
    if (segments.empty()) {
        return usage();
    }
    if (engine != "google" && engine != "openai" && engine != "anthropic") {
        print("--engine 只能是 google、openai 或 anthropic");
        return 2;
    }
    if (!baseUrl.empty()) {
        llm.baseUrl = baseUrl;
    }
    if (!model.empty()) {
        llm.model = model;
    }
    tmw::net::AnthropicTranslator::Options claude;
    claude.stream = llm.stream;
    if (!baseUrl.empty()) {
        claude.baseUrl = baseUrl;
    }
    if (!model.empty()) {
        claude.model = model;
    }
    const tmw::core::TranslateRequest request{
        language, "zh-TW", {}, tmw::core::glossaryFor(segments, glossary)};
    if (printRequest) {
        print(tmw::net::buildMessagesRequest(claude, request, segments, segments.size() > 1));
        return 0;
    }

    const tmw::core::SteadyClock clock;
    auto http = std::make_shared<tmw::net::CprHttpClient>();
    tmw::net::GoogleTranslator google(http, clock);
    if (!keyVariable.empty()) {
        // 金鑰只從環境變數讀，不會出現在命令列或記錄中
        std::size_t length = 0;
        char* value = nullptr;
        if (_dupenv_s(&value, &length, keyVariable.c_str()) == 0 && value != nullptr) {
            llm.apiKey = value;
            free(value);
        } else {
            print("讀不到環境變數 " + keyVariable);
            return 2;
        }
    }
    const auto onSegment = [](std::size_t index, const std::string& text) {
        print("  [第 " + std::to_string(index + 1) + " 段收到] " + text);
    };
    tmw::net::OpenAiTranslator openai(http, llm);
    openai.setOnSegment(onSegment);
    claude.apiKey = llm.apiKey;
    tmw::net::AnthropicTranslator anthropic(http, claude);
    anthropic.setOnSegment(onSegment);
    tmw::core::ITranslator& translator =
        engine == "google"      ? static_cast<tmw::core::ITranslator&>(google)
        : engine == "anthropic" ? static_cast<tmw::core::ITranslator&>(anthropic)
                                : openai;

    const auto start = std::chrono::steady_clock::now();
    try {
        const std::vector<std::string> out =
            translator.translate(segments, request, std::stop_token{});
        for (std::size_t i = 0; i < out.size(); ++i) {
            print(segments[i] + "  →  " + out[i]);
        }
    } catch (const tmw::core::TranslatorError& error) {
        print("失敗（" + tmw::core::describeTranslateError(error.kind()) + "）：" + error.what());
        return 1;
    } catch (const std::exception& error) {
        print(std::string("失敗：") + error.what());
        return 1;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    print("共 " + std::to_string(elapsed.count()) + " ms");
    return 0;
}
