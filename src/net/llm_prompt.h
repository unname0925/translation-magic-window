// LLM 翻譯引擎共用的部分：提示詞、使用者訊息、HTTP 錯誤的分類（design.md 4.5）。
//
// OpenAI 相容格式（net/openai_translator）和 Claude 原生 API（net/anthropic_translator）
// 送出的文字一模一樣，換引擎時譯文的差別只來自模型本身，不來自提示詞。
#pragma once

#include <span>
#include <string>

#include "core/translator.h"
#include "net/http_client.h"

namespace tmw::net {

// 系統訊息。asJsonArray：整批翻、要求和 segments 等長的 JSON 字串陣列；
// false：只剩一段時的退路，直接要譯文。有專有名詞時才在 M0-12 評測過的原句後面加一句。
std::string llmSystemPrompt(const core::TranslateRequest& request, bool asJsonArray);

// 使用者訊息。整批時是 {"source_lang", "context", "glossary", "segments"} 的 JSON，
// 單段時就是那一段文字本身。
std::string llmUserMessage(const core::TranslateRequest& request,
                           std::span<const std::string> segments, bool asJsonArray);

// 失敗的 HTTP 回應屬於哪一種錯誤：連不上和 5xx 是 Network、429 是 RateLimited，
// 其餘的 4xx（金鑰、模型名稱、被擋）是 Rejected——重送同樣的內容不會變好。
core::TranslateError classifyHttpFailure(const HttpResponse& response);

// 給使用者看的失敗原因。回應內容可能含有原文，只留狀態碼和 API 自己的錯誤說明
// （大多數服務放在 error.message，DeepL 放在最上層的 message）。
std::string describeHttpFailure(const HttpResponse& response);

}  // namespace tmw::net
