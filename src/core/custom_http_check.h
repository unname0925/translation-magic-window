// 自訂 HTTP 範本引擎（net/custom_http_translator）的範本檢查。純字串的檢查放在 core，
// 設定視窗（ui）和引擎（net）都用同一個，判斷才不會不一致。
#pragma once

#include <string>
#include <string_view>

namespace tmw::core {

// 範本有沒有問題（存檔前檢查）。沒問題回傳空字串。
std::string customHttpProblem(std::string_view url, std::string_view headers,
                              std::string_view bodyTemplate, std::string_view responsePath);

}  // namespace tmw::core
