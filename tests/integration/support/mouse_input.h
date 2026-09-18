#pragma once

#include <windows.h>

#include "core/geometry.h"

namespace tmw::test {

// 模擬真實的滑鼠操作（整合測試 IT-02、IT-03、IT-07 使用）。
//
// - 游標位置用 SetCursorPos 設定：實體像素座標，沒有 SendInput 絕對座標的換算誤差。
// - 按鍵用 SendInput 送出，並等到系統真的處理完才回傳，避免和下一次移動的順序錯亂。
// - 左右鍵對調（左手模式）時，送出的是實際對應到「主要按鍵」的那一顆。
// - 解構時放開還按著的按鍵，並把游標放回測試開始前的位置。
//
// 會移動你的滑鼠：測試執行期間請不要操作滑鼠。
class MouseSimulator {
public:
    MouseSimulator();
    ~MouseSimulator();

    MouseSimulator(const MouseSimulator&) = delete;
    MouseSimulator& operator=(const MouseSimulator&) = delete;

    // 把游標移到 point（螢幕座標、實體像素）。游標沒有到達時回傳 false
    // （例如被 ClipCursor 限制住）。
    bool moveTo(core::PointI point);

    // 按下／放開主要按鍵。系統在逾時前沒有處理完時回傳 false。
    bool press();
    bool release();

    // 在 point 按一下主要按鍵。
    bool click(core::PointI point);

    // 按住主要按鍵，從 from 拖到 to 再放開，用來拖動或縮放別的程式的視窗。
    // targetThreadId 是視窗所屬的執行緒：按下後會等它進入拖動／縮放迴圈才開始移動，
    // 放開後也會等它離開迴圈，所以回傳時視窗已經在最終位置。
    bool drag(core::PointI from, core::PointI to, DWORD targetThreadId);

private:
    bool sendButton(bool down);

    POINT original_{};
    bool pressed_ = false;
};

}  // namespace tmw::test
