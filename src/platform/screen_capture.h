#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

#include "core/geometry.h"
#include "core/image.h"

namespace tmw::platform {

struct CaptureStats {
    std::uint64_t framesArrived = 0;  // 系統送來的畫面數
    std::uint64_t sessionsStarted = 0;
    bool systemThrottling = false;  // 系統是否支援 MinUpdateInterval 節流
};

// 用 Windows.Graphics.Capture 擷取螢幕畫面（見 docs/design.md 4.2）。
//
// - 擷取整個螢幕，需要時才在 GPU 上裁切出指定範圍再讀回 CPU。
// - 不含滑鼠游標；Windows 11 上不顯示黃色的擷取邊框。
// - 由系統依 minFrameInterval 節流（MinUpdateInterval，Windows 11 24H2 起），
//   高更新率螢幕也不會每秒送來上百張畫面。每張送來的畫面都會保留，
//   不會自行丟棄，否則畫面靜止後會停在過時的內容。
// - D3D 裝置遺失時自動重建；螢幕設定改變或從睡眠恢復時，由呼叫端呼叫 reset()。
//
// 設定了 WDA_EXCLUDEFROMCAPTURE 的視窗（例如透鏡）不會出現在擷取結果中。
// 除了畫面回呼在系統的背景執行緒上執行之外，所有公開方法都應該從同一個執行緒呼叫。
class ScreenCapture {
public:
    struct Options {
        std::chrono::milliseconds minFrameInterval{100};
        // 是否把滑鼠游標畫進擷取結果。正式程式一律是 false；
        // 只有整合測試 IT-07 的正向對照會設成 true，證明測試抓得出游標。
        bool captureCursor = false;
    };

    // 建立 D3D 裝置。系統不支援螢幕擷取或建立裝置失敗時丟出 std::runtime_error。
    explicit ScreenCapture(Options options);
    ScreenCapture();
    ~ScreenCapture();

    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    // 讀取螢幕上 screenRect 範圍目前的畫面（實體像素座標）。
    // 只擷取範圍中心點所在的螢幕，超出該螢幕的部分會被裁掉。
    // 第一次擷取某個螢幕時，最多等待 timeout 讓第一張畫面送達。
    // 失敗時回傳 std::nullopt（例如中心點不在任何螢幕上、逾時、裝置錯誤）。
    std::optional<core::ImageBgra> readRegion(
        const core::RectI& screenRect,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{1000});

    // 讀取 screenRect 範圍的縮圖：在 GPU 上逐級縮小一半（mipmap），
    // 取第一個最長邊不超過 maxSide 的等級再讀回，讀回的資料量只有原圖的幾十分之一。
    // 用於變化偵測。範圍本來就夠小時直接讀回原圖。
    std::optional<core::ImageBgra> readThumbnail(
        const core::RectI& screenRect, int maxSide,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{1000});

    // 丟掉目前的擷取工作階段，下次 readRegion 時重建。
    // 螢幕設定改變（WM_DISPLAYCHANGE）或從睡眠恢復時呼叫。
    void reset();

    CaptureStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tmw::platform
