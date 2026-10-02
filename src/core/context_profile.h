// 情境模式（M2-06，design.md 4.9）：漫畫、遊戲、網頁各自記住一組設定，切換時一次換好。
//
// 設定檔最上層的 mangaMode、gameMode、ocrLanguage 等一直是「現在生效的值」；
// 選了情境之後，切走時把現在的值存回那個情境，切到新情境時載入它記住的值。
// 這樣舊的設定檔不用轉換，沒選情境（profile 是空字串）時行為和以前完全一樣。
#pragma once

#include <map>
#include <string>
#include <string_view>

namespace tmw::core {

// 一個情境記住的設定
struct ProfileValues {
    bool mangaMode = false;
    bool gameMode = false;
    std::string ocrLanguage = "auto";
    // 碰到透鏡邊緣、被切掉一部分的句子不翻（design.md 4.4）
    bool dropEdgeBlocks = true;
    // 畫面停下來多久才處理（design.md 4.3 的表格）
    int settleMs = 400;

    friend bool operator==(const ProfileValues&, const ProfileValues&) = default;
};

// 內建的三種情境："manga"、"game"、"web"
inline constexpr std::string_view kProfileIds[] = {"manga", "game", "web"};

// 內建情境的預設值（design.md 4.3、4.9）。不認得的 id 回傳一般的預設值。
ProfileValues defaultProfile(std::string_view id);

// 給使用者看的名稱：「漫畫」「遊戲」「網頁」
std::string profileName(std::string_view id);

}  // namespace tmw::core
