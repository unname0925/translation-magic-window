#pragma once

#include <windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <vector>

#include "core/geometry.h"
#include "core/image.h"

namespace tmw::test {

// 整合測試用的最上層視窗。
//
// Pattern 模式：每個像素的顏色都由它在視窗中的座標決定（見 expectedPixel），
// 所以從擷取結果就能反推出位置，座標偏移 1 個像素也會被發現。
//
// 視窗會記錄收到的滑鼠按鍵事件（見 mouseEvents），用來測試透鏡中間的點擊穿透。
// 點擊不會讓它變成前景視窗，測試執行時不會搶走你的鍵盤焦點。
class TestWindow {
public:
    enum class Mode { Pattern, Solid, Animated };

    struct MouseEvent {
        UINT message = 0;          // WM_LBUTTONDOWN、WM_LBUTTONUP 等
        core::PointI clientPoint;  // 視窗內的座標
    };

    struct Options {
        Mode mode = Mode::Pattern;
        COLORREF color = RGB(255, 0, 0);  // Solid 模式的顏色
        bool excludeFromCapture = false;
    };

    TestWindow(core::RectI screenRect, Options options);
    explicit TestWindow(core::RectI screenRect);
    ~TestWindow();

    TestWindow(const TestWindow&) = delete;
    TestWindow& operator=(const TestWindow&) = delete;

    HWND hwnd() const { return hwnd_; }
    const core::RectI& rect() const { return rect_; }

    void setSolidColor(COLORREF color);

    const std::vector<MouseEvent>& mouseEvents() const { return mouseEvents_; }
    void clearMouseEvents() { mouseEvents_.clear(); }

    // Pattern 模式下，視窗內 (x, y) 的顏色（B, G, R）。x、y 必須小於 4096。
    static std::array<std::uint8_t, 3> expectedPixel(int x, int y);

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void paint(HDC dc);

    HWND hwnd_ = nullptr;
    core::RectI rect_;
    Options options_;
    std::vector<std::uint32_t> patternPixels_;
    std::vector<MouseEvent> mouseEvents_;
    unsigned animationFrame_ = 0;
};

// 擷取結果中，和 Pattern 模式預期顏色不同的像素數。(offsetX, offsetY)
// 是擷取範圍的左上角在測試視窗內的位置。
int patternMismatches(const core::ImageBgra& image, int offsetX, int offsetY);

// 處理這個執行緒的視窗訊息一段時間（讓測試視窗重繪、計時器觸發）。
void pumpMessages(std::chrono::milliseconds duration);

// 一邊處理視窗訊息，一邊等到 condition 成立。逾時回傳 false。
bool waitUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout);

// 等到桌面合成（DWM）把目前的視窗內容畫到螢幕上。
void waitForComposition();

}  // namespace tmw::test
