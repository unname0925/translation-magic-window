#include "support/mouse_input.h"

#include <chrono>

#include "support/test_window.h"

namespace tmw::test {
namespace {

using namespace std::chrono_literals;

constexpr auto kInputTimeout = 2000ms;
constexpr int kDragSteps = 8;

bool isAnyButtonDown() {
    // GetAsyncKeyState 看的是實體按鍵還是對調後的邏輯按鍵，文件說法不一，所以兩顆都看
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
           (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
}

bool isInMoveSizeLoop(DWORD threadId) {
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    return GetGUIThreadInfo(threadId, &info) && (info.flags & GUI_INMOVESIZE) != 0;
}

}  // namespace

MouseSimulator::MouseSimulator() {
    GetCursorPos(&original_);
}

MouseSimulator::~MouseSimulator() {
    if (pressed_) {
        release();
    }
    SetCursorPos(original_.x, original_.y);
}

bool MouseSimulator::moveTo(core::PointI point) {
    if (!SetCursorPos(point.x, point.y)) {
        return false;
    }
    POINT actual{};
    return GetCursorPos(&actual) && actual.x == point.x && actual.y == point.y;
}

bool MouseSimulator::press() {
    return sendButton(true);
}

bool MouseSimulator::release() {
    return sendButton(false);
}

bool MouseSimulator::click(core::PointI point) {
    return moveTo(point) && press() && release();
}

bool MouseSimulator::drag(core::PointI from, core::PointI to, DWORD targetThreadId) {
    if (!moveTo(from) || !press()) {
        return false;
    }
    if (!waitUntil([&] { return isInMoveSizeLoop(targetThreadId); }, kInputTimeout)) {
        release();
        return false;
    }
    for (int step = 1; step <= kDragSteps; ++step) {
        const core::PointI point{from.x + (to.x - from.x) * step / kDragSteps,
                                 from.y + (to.y - from.y) * step / kDragSteps};
        if (!moveTo(point)) {
            release();
            return false;
        }
        // 讓對方的拖動迴圈處理這次移動
        pumpMessages(15ms);
    }
    if (!release()) {
        return false;
    }
    return waitUntil([&] { return !isInMoveSizeLoop(targetThreadId); }, kInputTimeout);
}

bool MouseSimulator::sendButton(bool down) {
    // SendInput 送的是實體按鍵；左右鍵對調時，主要按鍵是實體的右鍵
    const bool swapped = GetSystemMetrics(SM_SWAPBUTTON) != 0;
    INPUT input{};
    input.type = INPUT_MOUSE;
    if (swapped) {
        input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    } else {
        input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    }
    if (SendInput(1, &input, sizeof(input)) != 1) {
        return false;
    }
    pressed_ = down;
    return waitUntil([down] { return isAnyButtonDown() == down; }, kInputTimeout);
}

}  // namespace tmw::test
