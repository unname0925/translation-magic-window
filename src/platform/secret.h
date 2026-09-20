// API 金鑰的保護（見 docs/design.md 4.5「金鑰保管」）。
//
// 用 Windows 的 DPAPI（CryptProtectData）以「目前使用者」的身分加密，再轉成 base64 存進設定檔。
// 換成別的使用者或別台電腦就解不開；設定檔被看到也拿不到金鑰。
// 加密後的字串只在設定檔中出現，不會寫進記錄檔或除錯傾印。
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace tmw::platform {

// 加密並轉成 base64。空字串會原樣回傳空字串（表示「沒有設定金鑰」）。
std::string encryptSecret(std::string_view plaintext);

// 解開 encryptSecret 的結果。內容不是這台電腦、這個使用者加密的（或被改過）時回傳 nullopt。
std::optional<std::string> decryptSecret(std::string_view encrypted);

}  // namespace tmw::platform
