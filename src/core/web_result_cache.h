// 網頁漫畫的翻譯結果存在硬碟上：同一章重新整理、隔天再看、瀏覽器重開，同一張圖都不必再做
// OCR 和翻譯，直接把上次送給擴充功能的結果再送一次。
//
// 鍵是「這張圖的像素」加上會影響結果的條件（辨識語言、擬聲字開關、翻譯引擎和程式版本等，
// 由呼叫端組成 tag）。一個結果一個檔案，總大小超過上限時刪掉最久沒用到的。
// 檔案裡是有版權的漫畫的譯文，放在使用者自己的資料資料夾裡，不上傳。
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "core/image.h"

namespace tmw::core {

// 圖片像素的雜湊（64 位元 FNV-1a，每次執行都一樣）。一張 400 萬像素的圖約十幾毫秒
std::uint64_t hashPixels(const ImageBgra& image);

class WebResultCache {
public:
    WebResultCache(std::filesystem::path directory, std::uint64_t maxBytes);

    // 快取的鍵（16 個十六進位字元，也是檔名）
    static std::string makeKey(std::uint64_t pixelHash, int width, int height,
                               std::string_view language, bool soundEffects, std::string_view tag);

    // 找到時回傳當時存的回覆（擴充功能的結果訊息，id 要換成這次的），並記下「剛用過」
    std::optional<std::string> find(const std::string& key);
    void store(const std::string& key, const std::string& reply);

    std::uint64_t totalBytes() const { return totalBytes_; }

private:
    struct Entry {
        std::uint64_t size = 0;
        std::filesystem::file_time_type used;
    };

    void loadIndex();
    void evict();
    std::filesystem::path pathOf(const std::string& key) const;

    std::filesystem::path directory_;
    std::uint64_t maxBytes_ = 0;
    bool loaded_ = false;
    std::map<std::string, Entry> entries_;
    std::uint64_t totalBytes_ = 0;
};

}  // namespace tmw::core
