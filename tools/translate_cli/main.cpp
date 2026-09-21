// 手動實測翻譯引擎：把命令列上的每一段文字翻成繁體中文並印出來。
//
//   tmw_translate_cli --engine google --lang ja こんにちは セーブ
//
// 用途是確認引擎真的能連上端點（單元測試用的是錄好的回應），以及看一眼譯文的品質。
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <exception>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include "core/clock.h"
#include "core/translator.h"
#include "net/cpr_http_client.h"
#include "net/google_translator.h"
#include "platform/text_encoding.h"

namespace {

void print(const std::string& utf8) {
    std::fputs(utf8.c_str(), stdout);
    std::fputc('\n', stdout);
}

int usage() {
    print("用法：tmw_translate_cli [--engine google] [--lang ja|en|ko|auto] 文字...");
    return 2;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    // 主控台用 UTF-8 才看得到中文和日文
    SetConsoleOutputCP(CP_UTF8);

    std::string engine = "google";
    std::string language = "auto";
    std::vector<std::string> segments;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = tmw::platform::wideToUtf8(argv[i]);
        if (argument == "--engine" && i + 1 < argc) {
            engine = tmw::platform::wideToUtf8(argv[++i]);
        } else if (argument == "--lang" && i + 1 < argc) {
            language = tmw::platform::wideToUtf8(argv[++i]);
        } else if (argument == "--help" || argument == "-h") {
            return usage();
        } else {
            segments.push_back(argument);
        }
    }
    if (segments.empty()) {
        return usage();
    }
    if (engine != "google") {
        print("目前只支援 --engine google");
        return 2;
    }

    const tmw::core::SteadyClock clock;
    auto http = std::make_shared<tmw::net::CprHttpClient>();
    tmw::net::GoogleTranslator translator(http, clock);

    const auto start = std::chrono::steady_clock::now();
    try {
        const std::vector<std::string> out = translator.translate(
            segments, tmw::core::TranslateRequest{language, "zh-TW", {}, {}}, std::stop_token{});
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
