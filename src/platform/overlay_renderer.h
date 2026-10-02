// 把 core/overlay_plan 規劃好的譯文畫成一張圖（M3-03，design.md 4.8）。
//
// 用 Direct2D／DirectWrite 畫在記憶體裡的點陣圖上，不碰視窗：覆蓋層視窗拿這張圖去更新，
// 測試也能直接比對畫出來的像素（M3-05）。
// - 每一段先用背景色填滿，再畫譯文
// - 字級自動調整：找出整段放得進框的最大字級，但不大於原文的字
// - 直排的段落直排（由上到下、由右到左），標點用直排字形
#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>

#include "core/geometry.h"
#include "core/image.h"
#include "core/overlay_item.h"

namespace tmw::platform {

class OverlayRenderer {
public:
    // 建立 Direct2D／DirectWrite 失敗時丟出 std::runtime_error
    OverlayRenderer();
    ~OverlayRenderer();

    OverlayRenderer(const OverlayRenderer&) = delete;
    OverlayRenderer& operator=(const OverlayRenderer&) = delete;

    // 預乘 alpha 的 BGRA（UpdateLayeredWindow 要的格式）。沒有譯文的地方完全透明。
    core::ImageBgra render(core::SizeI size, std::span<const core::OverlayItem> items);

    // 譯文用這個字型檔（M4-04 的內建字型，從檔案載入、不安裝到系統）。
    // 空路徑代表用系統的微軟正黑體。檔案讀不了時回傳 false，並改回微軟正黑體。
    bool setFont(const std::filesystem::path& file);
    // 目前用的字型家族名稱；空字串代表微軟正黑體
    std::wstring fontFamily() const;

    // 這一段用的字級（像素）。框太小連最小字級都放不下時回傳最小字級，畫的時候超出的部分裁掉。
    float fitFontSize(const core::OverlayItem& item);

    static constexpr float kMinFontSize = 8.0f;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tmw::platform
