// OpenAI 相容格式的 LLM 引擎（見 docs/design.md 4.5）。
//
// 同一個格式可以接 OpenAI、Gemini、DeepSeek、OpenRouter，以及本機的 Ollama、llama.cpp、
// LM Studio，所以只要一份實作。使用者填了金鑰時這是預設引擎（M0-12：三個 LLM 的品質
// 明顯優於 Google，彼此之間沒有顯著差異）。
//
// 一個畫面的段落一次送出（省時間也省錢），要求輸出等長的 JSON 字串陣列。
// 串流開著時邊收邊解析，收完一段就可以先顯示一段。
#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
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

class OpenAiTranslator final : public core::ITranslator {
public:
    struct Options {
        // 設定檔和記錄檔中的識別字。可以同時設定多個 LLM 引擎，所以不寫死。
        std::string id = "openai";
        // 結尾不用加 /chat/completions。填 localhost 會自動換成 127.0.0.1（M0-12）。
        std::string baseUrl = "https://api.openai.com/v1";
        std::string model = "gpt-4o-mini";
        std::string apiKey;  // 本機服務通常不用
        double temperature = 0.2;
        bool stream = true;
        std::chrono::milliseconds timeout{60000};
    };

    // 串流時每收完一段譯文就呼叫一次。index 是在這一批中的位置；整批重試時會從 0 再來一次，
    // 同一個 index 後來的內容要覆蓋先前的。逐段重送的結果不會經過這裡（那時已經沒有陣列可解），
    // 最終結果一律以 translate() 的回傳值為準。
    using OnSegment = std::function<void(std::size_t index, const std::string& text)>;

    OpenAiTranslator(std::shared_ptr<IHttpClient> http, Options options);

    std::string id() const override { return options_.id; }
    bool supportsBatch() const override { return true; }

    std::vector<std::string> translate(std::span<const std::string> segments,
                                       const core::TranslateRequest& request,
                                       std::stop_token cancel) override;

    // 設定之後，串流回來的每一段都會即時通知（給結果視窗邊翻邊顯示用）
    void setOnSegment(OnSegment callback) { onSegment_ = std::move(callback); }

private:
    // 整批：要求 JSON 陣列
    std::vector<std::string> requestArray(std::span<const std::string> segments,
                                          const core::TranslateRequest& request,
                                          std::stop_token cancel);
    // 單段：不要 JSON，只要譯文（最後的退路，模型偶爾會輸出沒有逸出的引號）
    std::string requestPlain(const std::string& segment, const core::TranslateRequest& request,
                             std::stop_token cancel);

    std::string completionsUrl() const;
    std::string send(const std::string& body, std::stop_token cancel, bool reportSegments);

    std::shared_ptr<IHttpClient> http_;
    Options options_;
    OnSegment onSegment_;
};

// 從一則串流事件中取出這次新增的文字（choices[0].delta.content）。
// 不是文字的事件（角色、結束原因、用量統計）回傳空字串。
std::string streamedContent(std::string_view event);

// 從一次完整的回應中取出訊息內容（choices[0].message.content）。格式不符時回傳 nullopt。
std::optional<std::string> completionContent(std::string_view body);

// 送出去的請求內容。抽出來是為了測試提示詞和參數，不必真的連線。
std::string buildChatRequest(const OpenAiTranslator::Options& options,
                             const core::TranslateRequest& request,
                             std::span<const std::string> segments, bool asJsonArray);

}  // namespace tmw::net
