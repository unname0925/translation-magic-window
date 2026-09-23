// tmw_capture_bench：量測「透鏡閒著的時候到底花掉多少 CPU」（M1-17）。
//
//   tmw_capture_bench [--seconds N] [--interval MS] [--animate] [--no-thumbnail]
//
// 模擬主程式閒置時的迴圈：每 interval 毫秒看一次有沒有新畫面，有的話讀回透鏡範圍的縮圖
// 做變化偵測。驗收條件是「其他視窗持續變化、透鏡底下沒變時，單一核心 CPU < 1%」，
// 所以 --animate 會在螢幕角落開一個一直重畫的視窗，而量測用的範圍刻意和它不重疊。
//
// --no-thumbnail 只收畫面、不讀縮圖，用來分辨「成本在系統送畫面」還是「在我們讀回來」。
#include <windows.h>

#include <objbase.h>

#include <chrono>
#include <cstdio>
#include <exception>
#include <optional>
#include <string>
#include <thread>

#include "core/change_detection.h"
#include "platform/capture_frame_source.h"
#include "platform/screen_capture.h"

namespace {

struct Arguments {
    int seconds = 20;
    int intervalMs = 100;
    bool animate = true;
    bool thumbnail = true;
    // --centre：動畫視窗放在螢幕中央（透鏡預設在那裡），當作「測得出差別」的反向對照
    bool centre = false;
};

Arguments parseArguments(int argc, wchar_t** argv) {
    Arguments args;
    for (int i = 1; i < argc; ++i) {
        const std::wstring option = argv[i];
        if (option == L"--seconds" && i + 1 < argc) {
            args.seconds = std::stoi(argv[++i]);
        } else if (option == L"--interval" && i + 1 < argc) {
            args.intervalMs = std::stoi(argv[++i]);
        } else if (option == L"--no-animate") {
            args.animate = false;
        } else if (option == L"--centre") {
            args.centre = true;
        } else if (option == L"--no-thumbnail") {
            args.thumbnail = false;
        }
    }
    return args;
}

// 這個程序用掉的 CPU 時間（使用者 + 核心）
double processCpuSeconds() {
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) == FALSE) {
        return 0.0;
    }
    const auto toSeconds = [](const FILETIME& time) {
        return ((static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime) / 1e7;
    };
    return toSeconds(kernel) + toSeconds(user);
}

// 螢幕角落一直重畫的視窗，用來製造「其他地方一直在變」
class AnimatedWindow {
public:
    explicit AnimatedWindow(bool centre) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"TranslationMagicWindow.CaptureBench";
        RegisterClassExW(&windowClass);
        const int width = 320;
        const int height = 200;
        const int left = centre ? (GetSystemMetrics(SM_CXSCREEN) - width) / 2 : 0;
        const int top = centre ? (GetSystemMetrics(SM_CYSCREEN) - height) / 2 : 0;
        hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                windowClass.lpszClassName, L"capture bench", WS_POPUP, left, top,
                                width, height, nullptr, nullptr, windowClass.hInstance, nullptr);
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }

    ~AnimatedWindow() {
        if (hwnd_ != nullptr) {
            DestroyWindow(hwnd_);
        }
    }

    AnimatedWindow(const AnimatedWindow&) = delete;
    AnimatedWindow& operator=(const AnimatedWindow&) = delete;

    // 畫一格不一樣的顏色。每次呼叫畫面都會變，系統就會一直送新畫面過來。
    void step() {
        if (hwnd_ == nullptr) {
            return;
        }
        const HDC dc = GetDC(hwnd_);
        RECT rect{};
        GetClientRect(hwnd_, &rect);
        const HBRUSH brush = CreateSolidBrush(RGB(frame_ * 7 % 256, frame_ * 13 % 256, 60));
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
        ReleaseDC(hwnd_, dc);
        ++frame_;
    }

    RECT rect() const {
        RECT rect{};
        if (hwnd_ != nullptr) {
            GetWindowRect(hwnd_, &rect);
        }
        return rect;
    }

private:
    HWND hwnd_ = nullptr;
    int frame_ = 0;
};

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const Arguments args = parseArguments(argc, argv);
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    try {
        std::optional<AnimatedWindow> animation;
        if (args.animate) {
            animation.emplace(args.centre);
        }

        // 量測範圍放在螢幕右下角，和左上角那個一直重畫的視窗不重疊
        const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        const tmw::core::RectI region{screenWidth - 760, screenHeight - 460, screenWidth - 40,
                                      screenHeight - 55};

        tmw::platform::ScreenCapture capture(
            {.minFrameInterval = std::chrono::milliseconds{args.intervalMs}});
        tmw::platform::CaptureFrameSource frames(capture);

        std::uint64_t ticks = 0;
        std::uint64_t reads = 0;
        std::uint64_t skipped = 0;
        std::optional<std::uint64_t> lastSerial;
        std::optional<tmw::core::GrayImage> reference;

        // 先跑一次讓工作階段建立起來，建立的成本不算進去
        frames.thumbnail(region);
        const double cpuStart = processCpuSeconds();
        const auto wallStart = std::chrono::steady_clock::now();

        while (std::chrono::steady_clock::now() - wallStart < std::chrono::seconds(args.seconds)) {
            if (animation) {
                animation->step();
            }
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }

            ++ticks;
            const std::uint64_t serial = frames.frameSerial();
            if (lastSerial && *lastSerial == serial) {
                ++skipped;
            } else if (args.thumbnail) {
                std::optional<tmw::core::GrayImage> thumbnail = frames.thumbnail(region);
                if (thumbnail) {
                    lastSerial = serial;
                    ++reads;
                    if (reference) {
                        (void)tmw::core::contentChanged(*reference, *thumbnail, {});
                    }
                    reference = std::move(thumbnail);
                }
            } else {
                lastSerial = serial;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(args.intervalMs));
        }

        const double cpuUsed = processCpuSeconds() - cpuStart;
        const double wallUsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();
        const tmw::platform::CaptureStats stats = capture.stats();

        std::printf("量測 %.1f 秒，每 %d 毫秒一次\n", wallUsed, args.intervalMs);
        std::printf("系統送來 %llu 張畫面，我們讀了 %llu 次縮圖，跳過 %llu 次（沒有新畫面）\n",
                    static_cast<unsigned long long>(stats.framesArrived),
                    static_cast<unsigned long long>(reads),
                    static_cast<unsigned long long>(skipped));
        std::printf("CPU 時間 %.3f 秒 → 單一核心 %.2f%%\n", cpuUsed, cpuUsed / wallUsed * 100.0);
        std::printf("（%llu 次迴圈；讀縮圖：%s，角落動畫：%s）\n",
                    static_cast<unsigned long long>(ticks), args.thumbnail ? "有" : "沒有",
                    args.animate ? "有" : "沒有");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "失敗：%s\n", error.what());
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        return 1;
    }

    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return 0;
}
