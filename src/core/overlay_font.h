// 譯文覆蓋層可以選的字型（M4-04，design.md 4.8「內建字型」）。
//
// 內建字型由 tools/fetch_models 下載到 models/fonts（OFL 授權，可以隨程式發布），
// 從檔案載入、不安裝到系統。設定檔存的是 id；空字串（或不認得的 id）用系統的微軟正黑體。
#pragma once

#include <array>
#include <string_view>

namespace tmw::core {

struct OverlayFont {
    std::string_view id;     // 設定檔裡的值
    std::string_view label;  // 設定視窗顯示的名稱
    std::string_view file;   // models/fonts 裡的檔名；空的代表系統字型
};

inline constexpr std::array<OverlayFont, 4> kOverlayFonts{{
    {"", "微軟正黑體（系統內建）", ""},
    {"noto-sans", "思源黑體", "NotoSansTC-Variable.ttf"},
    {"noto-serif", "思源宋體（明體）", "NotoSerifTC-Variable.ttf"},
    {"huninn", "jf open 粉圓（圓體）", "jf-openhuninn-2.1.ttf"},
}};

// 這個 id 的字型檔名；空字串或不認得的 id 回傳空字串（用系統字型）
constexpr std::string_view overlayFontFile(std::string_view id) {
    for (const OverlayFont& font : kOverlayFonts) {
        if (font.id == id) {
            return font.file;
        }
    }
    return {};
}

}  // namespace tmw::core
