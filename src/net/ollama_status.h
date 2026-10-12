// 本機 Ollama 的模型有沒有整個放在顯示卡上。
//
// Ollama 只在載入模型的那一刻決定放多少層到顯示卡；那時顯示記憶體不夠（主程式的 OCR、背景修補
// 已經用了 3 GB 以上、或別的程式佔著），其餘的層就用 CPU 算，之後 30 分鐘（keep_alive）都是這樣。
// 2026-10-12 實測（RTX 4070、hy-mt2-tmw 5256 MB）：整個在顯示卡上時一章 19 頁約 77 秒、
// 多數頁 2～8 秒；只有 3.5～4.4 GB 在顯示卡上時一頁要 10～125 秒。
// 主程式之後長大不會把已經載入的模型擠出去。
// 所以要趁顯示記憶體還有空的時候載入（主程式一啟動），沒放滿的話趁沒在翻譯時卸載再載入一次。
#pragma once

#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

#include "net/http_client.h"

namespace tmw::net {

inline constexpr std::string_view kLocalOllama = "http://127.0.0.1:11434";

struct OllamaPlacement {
    std::uint64_t size = 0;   // 模型載入後總共占多少（位元組）
    std::uint64_t onGpu = 0;  // 其中放在顯示卡上的

    // 差不到 2% 算整個在顯示卡上
    bool fullyOnGpu() const { return onGpu + size / 50 >= size; }
};

// /api/ps 的回應裡這個模型的位置；沒載入時 nullopt。"hy-mt2-tmw" 和 "hy-mt2-tmw:latest" 算同一個
std::optional<OllamaPlacement> parseOllamaPlacement(std::string_view psJson,
                                                    std::string_view model);

// 問 Ollama（GET /api/ps）
std::optional<OllamaPlacement> ollamaPlacement(IHttpClient& http, std::string_view model,
                                               std::stop_token cancel);

// 載入模型並保留 keepAlive（POST /api/generate，不給提示詞只載入）；keepAlive 是 "0" 時卸載
bool setOllamaKeepAlive(IHttpClient& http, std::string_view model, std::string_view keepAlive,
                        std::stop_token cancel);

enum class OllamaPlacementAction {
    NotLoaded,     // 沒載入：不用管，下次翻譯時才載入
    Full,          // 整個在顯示卡上
    Reloaded,      // 本來沒放滿，卸載再載入後放滿了
    StillPartial,  // 卸載再載入後還是放不滿（顯示記憶體被別的程式佔著）
};

struct OllamaPlacementCheck {
    OllamaPlacementAction action = OllamaPlacementAction::NotLoaded;
    std::optional<OllamaPlacement> before;
    std::optional<OllamaPlacement> after;  // 重新載入過才有
};

// 看一下；沒放滿而且 mayReload 時卸載再載入（保留 keepAlive）
OllamaPlacementCheck checkOllamaPlacement(IHttpClient& http, std::string_view model,
                                          std::string_view keepAlive, bool mayReload,
                                          std::stop_token cancel);

}  // namespace tmw::net
