// 檢查有沒有新版本（M5-04 的前半：只通知，不自動下載安裝）。
//
// 讀 GitHub 上最新的正式發行版本（不含草稿和預覽版）。只讀公開資訊，不送出任何使用者的資料
// （GitHub 會看到 IP 位址，見 docs/privacy.md）。
#pragma once

#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

#include "net/http_client.h"

namespace tmw::net {

struct ReleaseInfo {
    std::string version;  // 「0.2.0」（去掉標籤開頭的 v）
    std::string url;      // 發行頁面，給使用者下載
};

// 「1.2.3」「v1.2.3」比較大小。看不懂的版本號一律當成「不是比較新」，不打擾使用者
bool isNewerVersion(std::string_view candidate, std::string_view current);

// 最新的正式發行版本。還沒有發行過（404）、連不上、回應看不懂時回傳 nullopt
std::optional<ReleaseInfo> fetchLatestRelease(IHttpClient& http, std::stop_token cancel);

inline constexpr std::string_view kLatestReleaseUrl =
    "https://api.github.com/repos/unname0925/translation-magic-window/releases/latest";

}  // namespace tmw::net
