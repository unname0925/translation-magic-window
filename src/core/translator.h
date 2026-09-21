// 翻譯引擎的介面和共用型別（見 docs/design.md 4.5）。
//
// core 不碰網路：實際的引擎（Google、LLM）住在 net 模組，這裡只定義它們要實作的介面，
// 測試時換成假引擎。
#pragma once

#include <map>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace tmw::core {

struct TranslateRequest {
    std::string srcLang;            // "ja" | "en" | "ko" | "auto"
    std::string dstLang = "zh-TW";  // 目前固定
    // 同一個透鏡最近幾組的原文和譯文，給 LLM 當上下文
    std::vector<std::pair<std::string, std::string>> context;
    std::map<std::string, std::string> glossary;  // 專有名詞表（M2 以後）
};

// 引擎失敗的原因。引擎鏈用它決定要不要改用下一個引擎（design.md 4.5 步驟 2）。
enum class TranslateError {
    Network,      // 連不上、逾時、HTTP 5xx
    RateLimited,  // HTTP 429、配額用完
    BadResponse,  // 回應格式錯誤、數量對不上
    Cancelled,    // 使用者移動透鏡或畫面又變了
    Unavailable,  // 沒有可用的引擎（全部都在暫停中）
};

std::string describeTranslateError(TranslateError error);

class TranslatorError : public std::runtime_error {
public:
    TranslatorError(TranslateError kind, const std::string& message)
        : std::runtime_error(message), kind_(kind) {}

    TranslateError kind() const noexcept { return kind_; }

private:
    TranslateError kind_;
};

class ITranslator {
public:
    virtual ~ITranslator() = default;

    // 設定檔和記錄檔裡用的識別字，例如 "google"、"openai"
    virtual std::string id() const = 0;

    // 能不能一次送出多段。不能的引擎由呼叫端逐段送出。
    virtual bool supportsBatch() const = 0;

    // 回傳的陣列長度必須和 segments 相同（引擎自己負責對齊，見 translation_alignment.h）。
    // 失敗時丟出 TranslatorError。
    virtual std::vector<std::string> translate(std::span<const std::string> segments,
                                               const TranslateRequest& request,
                                               std::stop_token cancel) = 0;
};

}  // namespace tmw::core
