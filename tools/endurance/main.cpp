// tmw_endurance：耐久測試（見 docs/execution-plan.md 5.7、M1-16）。
//
//   tmw_endurance [--minutes N] [--change-seconds N] [--sample-seconds N]
//                 [--settings <settings.json>] [--keep]
//
// 自己啟動主程式，在透鏡底下放一個每隔幾秒換一次內容的視窗，然後每隔一段時間記錄
// 主程式的記憶體、控制代碼數、GDI 物件數和 USER 物件數。任何一項**持續成長**就代表有洩漏。
//
// 結束時會自己判讀並印出一張小表：看的是「後半段比前半段多了多少」，
// 而不是「最後一筆比第一筆多了多少」——剛啟動時本來就會長一陣子（快取、模型、視窗）。
//
// 預設不接任何真的翻譯引擎（指向一個連不上的本機位址），理由是跑一兩個小時會把
// 線上引擎打爆。要連真的引擎就用 --settings 指定一份設定檔。
// 翻譯失敗時主程式照樣顯示原文並記錄原因，要驗的擷取、OCR、分段和結果視窗都還是會走到。
#include <windows.h>

#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr wchar_t kLensClassName[] = L"TranslationMagicWindow.Lens";
constexpr wchar_t kTargetClassName[] = L"TranslationMagicWindow.EnduranceTarget";

struct Arguments {
    int minutes = 60;
    int changeSeconds = 5;
    int sampleSeconds = 30;
    std::filesystem::path settings;
    bool keep = false;  // --keep：結束後保留資料夾（記錄檔要留著看）
};

Arguments parseArguments(int argc, wchar_t** argv) {
    Arguments args;
    for (int i = 1; i < argc; ++i) {
        const std::wstring option = argv[i];
        if (option == L"--minutes" && i + 1 < argc) {
            args.minutes = std::stoi(argv[++i]);
        } else if (option == L"--change-seconds" && i + 1 < argc) {
            args.changeSeconds = std::stoi(argv[++i]);
        } else if (option == L"--sample-seconds" && i + 1 < argc) {
            args.sampleSeconds = std::stoi(argv[++i]);
        } else if (option == L"--settings" && i + 1 < argc) {
            args.settings = argv[++i];
        } else if (option == L"--keep") {
            args.keep = true;
        }
    }
    return args;
}

// 主程式某一刻的資源用量
struct Sample {
    double minutes = 0.0;
    double workingSetMb = 0.0;
    double privateMb = 0.0;
    unsigned long handles = 0;
    unsigned long gdiObjects = 0;
    unsigned long userObjects = 0;
};

Sample sampleProcess(HANDLE process, double minutes) {
    Sample sample;
    sample.minutes = minutes;
    PROCESS_MEMORY_COUNTERS_EX memory{};
    if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                             sizeof(memory)) != FALSE) {
        sample.workingSetMb = static_cast<double>(memory.WorkingSetSize) / (1024 * 1024);
        sample.privateMb = static_cast<double>(memory.PrivateUsage) / (1024 * 1024);
    }
    DWORD handles = 0;
    if (GetProcessHandleCount(process, &handles) != FALSE) {
        sample.handles = handles;
    }
    sample.gdiObjects = GetGuiResources(process, GR_GDIOBJECTS);
    sample.userObjects = GetGuiResources(process, GR_USEROBJECTS);
    return sample;
}

// 每隔幾秒換一次內容的視窗。換完之後畫面要靜止，主程式才會判定「穩定」並真的去翻譯；
// 一直動的畫面永遠等不到穩定，整條管線就不會走到。
class TargetWindow {
public:
    TargetWindow() {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        windowClass.lpszClassName = kTargetClassName;
        RegisterClassExW(&windowClass);
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kTargetClassName,
                                L"endurance target", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr,
                                windowClass.hInstance, nullptr);
        font_ = CreateFontW(-28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    }

    ~TargetWindow() {
        if (font_ != nullptr) {
            DeleteObject(font_);
        }
        if (hwnd_ != nullptr) {
            DestroyWindow(hwnd_);
        }
    }

    TargetWindow(const TargetWindow&) = delete;
    TargetWindow& operator=(const TargetWindow&) = delete;

    bool valid() const { return hwnd_ != nullptr; }

    // 放到透鏡底下（在透鏡之後、其他視窗之上）
    void placeUnder(HWND lens) {
        RECT rect{};
        GetWindowRect(lens, &rect);
        SetWindowPos(hwnd_, lens, rect.left, rect.top, rect.right - rect.left,
                     rect.bottom - rect.top, SWP_NOACTIVATE);
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }

    // 換一段不一樣的日文，畫完就不再動
    void nextContent() {
        static const wchar_t* const lines[] = {
            L"今日はいい天気ですね",   L"約束の時間に間に合わない", L"この部屋は使われていない",
            L"彼女は何も言わなかった", L"明日また会いましょう",     L"それは知らなかった",
            L"手紙が届いていますよ",   L"もう時間がないんだ",
        };
        const HDC dc = GetDC(hwnd_);
        RECT rect{};
        GetClientRect(hwnd_, &rect);
        FillRect(dc, &rect, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        const HGDIOBJ previous = SelectObject(dc, font_);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(0, 0, 0));
        RECT where{40, 40, rect.right - 40, rect.bottom - 40};
        // 每次多畫一行不同的句子，文字量也跟著變，OCR 和分段都會走不同的路徑
        std::wstring text;
        for (int i = 0; i <= step_ % 4; ++i) {
            text += lines[(step_ + i) % std::size(lines)];
            text += L"\n";
        }
        DrawTextW(dc, text.c_str(), -1, &where, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, previous);
        ReleaseDC(hwnd_, dc);
        ++step_;
    }

private:
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    int step_ = 0;
};

void pumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

double averageOf(const std::vector<Sample>& samples, std::size_t from, std::size_t to,
                 double Sample::* field) {
    double total = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        total += samples[i].*field;
    }
    return to > from ? total / static_cast<double>(to - from) : 0.0;
}

double averageOf(const std::vector<Sample>& samples, std::size_t from, std::size_t to,
                 unsigned long Sample::* field) {
    double total = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        total += samples[i].*field;
    }
    return to > from ? total / static_cast<double>(to - from) : 0.0;
}

// 後半段比前半段多了多少。剛啟動時本來就會長一陣子，所以不拿第一筆來比。
template <typename Field>
void reportGrowth(const char* name, const std::vector<Sample>& samples, Field field,
                  double allowedGrowth, const char* unit, bool& leaked) {
    if (samples.size() < 4) {
        return;
    }
    const std::size_t half = samples.size() / 2;
    const double first = averageOf(samples, 0, half, field);
    const double second = averageOf(samples, half, samples.size(), field);
    const double growth = second - first;
    const bool bad = first > 0.0 && growth > allowedGrowth;
    leaked = leaked || bad;
    std::printf("%-12s 前半 %8.1f%s　後半 %8.1f%s　變化 %+7.1f%s  %s\n", name, first, unit, second,
                unit, growth, unit, bad ? "← 持續成長，像是洩漏" : "穩定");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const Arguments args = parseArguments(argc, argv);
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    const std::filesystem::path exe =
        std::filesystem::path(modulePath).parent_path() / L"TranslationMagicWindow.exe";
    if (!std::filesystem::exists(exe)) {
        std::fprintf(stderr, "找不到主程式：%ls\n", exe.c_str());
        return 2;
    }

    const std::filesystem::path dataDirectory =
        std::filesystem::temp_directory_path() /
        (L"tmw-endurance-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(dataDirectory);
    if (!args.settings.empty()) {
        std::error_code ignored;
        std::filesystem::copy_file(args.settings, dataDirectory / L"settings.json",
                                   std::filesystem::copy_options::overwrite_existing, ignored);
    } else {
        // 連不上的位址：翻譯會很快失敗，不會對線上引擎打上幾千次
        std::ofstream(dataDirectory / L"settings.json", std::ios::binary)
            << R"({"schemaVersion":1,"engines":[{"id":"openai-compatible",)"
            << R"("endpoint":"http://127.0.0.1:1/v1","model":"none","encryptedApiKey":""}]})"
            << "\n";
    }

    std::wstring commandLine =
        L"\"" + exe.wstring() + L"\" --data-dir \"" + dataDirectory.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                       &startup, &process) == FALSE) {
        std::fprintf(stderr, "啟動不了主程式\n");
        return 2;
    }

    // 等透鏡出現（防毒軟體第一次掃描新的執行檔會花 20 秒以上）
    HWND lens = nullptr;
    for (int waited = 0; waited < 60 && lens == nullptr; ++waited) {
        lens = FindWindowW(kLensClassName, nullptr);
        if (lens == nullptr) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    if (lens == nullptr) {
        std::fprintf(stderr, "主程式的透鏡沒有出現\n");
        TerminateProcess(process.hProcess, 1);
        return 2;
    }

    TargetWindow target;
    if (!target.valid()) {
        std::fprintf(stderr, "建不出目標視窗\n");
        TerminateProcess(process.hProcess, 1);
        return 2;
    }
    target.placeUnder(lens);

    std::printf("耐久測試開始：%d 分鐘，每 %d 秒換一次內容，每 %d 秒記錄一次\n", args.minutes,
                args.changeSeconds, args.sampleSeconds);
    std::printf("資料夾：%ls\n\n", dataDirectory.c_str());
    std::fflush(stdout);

    std::vector<Sample> samples;
    const auto start = std::chrono::steady_clock::now();
    const auto finish = start + std::chrono::minutes(args.minutes);
    auto nextChange = start;
    auto nextSample = start + std::chrono::seconds(args.sampleSeconds);

    while (std::chrono::steady_clock::now() < finish) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextChange) {
            target.nextContent();
            nextChange = now + std::chrono::seconds(args.changeSeconds);
        }
        if (now >= nextSample) {
            const double minutes = std::chrono::duration<double>(now - start).count() / 60.0;
            samples.push_back(sampleProcess(process.hProcess, minutes));
            const Sample& latest = samples.back();
            std::printf(
                "%6.1f 分：記憶體 %6.1f MB　私有 %6.1f MB　控制代碼 %4lu　GDI %4lu　USER %4lu\n",
                latest.minutes, latest.workingSetMb, latest.privateMb, latest.handles,
                latest.gdiObjects, latest.userObjects);
            std::fflush(stdout);
            nextSample = now + std::chrono::seconds(args.sampleSeconds);
        }
        pumpMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        DWORD exitCode = 0;
        if (GetExitCodeProcess(process.hProcess, &exitCode) != FALSE && exitCode != STILL_ACTIVE) {
            std::printf("\n主程式在第 %.1f 分鐘自己結束了（代碼 %lu）——這本身就是問題\n",
                        std::chrono::duration<double>(now - start).count() / 60.0, exitCode);
            break;
        }
    }

    bool leaked = false;
    std::printf("\n共 %zu 筆記錄\n", samples.size());
    // 容許值：一兩個小時下來，這個程度的變化都還在正常的波動範圍內
    reportGrowth("記憶體", samples, &Sample::workingSetMb, 30.0, " MB", leaked);
    reportGrowth("私有記憶體", samples, &Sample::privateMb, 30.0, " MB", leaked);
    reportGrowth("控制代碼", samples, &Sample::handles, 50.0, " 個", leaked);
    reportGrowth("GDI 物件", samples, &Sample::gdiObjects, 20.0, " 個", leaked);
    reportGrowth("USER 物件", samples, &Sample::userObjects, 20.0, " 個", leaked);
    std::printf("\n判讀：%s\n", leaked ? "有東西持續成長，要查" : "沒有持續成長的跡象");

    TerminateProcess(process.hProcess, 0);
    WaitForSingleObject(process.hProcess, 5000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    std::printf("記錄檔：%ls\\logs\n", dataDirectory.c_str());
    if (!args.keep) {
        std::error_code ignored;
        std::filesystem::remove_all(dataDirectory, ignored);
    }
    return leaked ? 1 : 0;
}
