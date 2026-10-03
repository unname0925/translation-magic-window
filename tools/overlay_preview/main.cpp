// tmw_overlay_preview：對圖片跑完整的流程（OCR → 翻譯 → 覆蓋層），把「譯文蓋在原文上」的樣子
// 存成 PNG（M3）。覆蓋層排除在螢幕擷取之外，截圖看不到它，要檢查效果就用這個。
//
//   tmw_overlay_preview [--settings <settings.json>] [--manga] [--language ja|en|ko|auto]
//                       [--font noto-sans|noto-serif|huninn]
//                       --output <資料夾> <圖片> [<圖片> ...]
//
// --settings：用這個設定檔的翻譯引擎（金鑰要是這台電腦、這個使用者加密的）。
//             不給的話用 Google（不用金鑰）。
// 每張圖輸出 <名字>.overlay.png，並把每一段的原文、譯文、蓋不蓋（和判斷用的數字）印出來。

#include <windows.h>

#include <objbase.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include "app/translation_setup.h"
#include "core/clock.h"
#include "core/furigana_readings.h"
#include "core/opencc_converter.h"
#include "core/overlay_font.h"
#include "core/overlay_plan.h"
#include "core/pipeline.h"
#include "ocr/lama_inpainter.h"
#include "ocr/ocr_service.h"
#include "platform/app_paths.h"
#include "platform/logging.h"
#include "platform/overlay_renderer.h"
#include "platform/png_file.h"
#include "platform/settings_file.h"
#include "platform/text_encoding.h"

namespace {

using namespace tmw;

struct Options {
    std::optional<std::filesystem::path> settings;
    bool manga = false;
    std::string language = "auto";
    std::string font;  // core/overlay_font 的 id
    std::filesystem::path output;
    std::vector<std::filesystem::path> images;
};

int usage() {
    std::fputs(
        "usage: tmw_overlay_preview [--settings <settings.json>] [--manga]\n"
        "                           [--language ja|en|ko|auto] [--font "
        "noto-sans|noto-serif|huninn]\n"
        "                           --output <dir> <image> ...\n",
        stderr);
    return 2;
}

std::optional<Options> parse(int argc, wchar_t** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::wstring option = argv[i];
        const auto next = [&]() -> const wchar_t* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (option == L"--settings") {
            const wchar_t* value = next();
            if (value == nullptr) {
                return std::nullopt;
            }
            options.settings = value;
        } else if (option == L"--manga") {
            options.manga = true;
        } else if (option == L"--language") {
            const wchar_t* value = next();
            if (value == nullptr) {
                return std::nullopt;
            }
            options.language = platform::wideToUtf8(value);
        } else if (option == L"--font") {
            const wchar_t* value = next();
            if (value == nullptr) {
                return std::nullopt;
            }
            options.font = platform::wideToUtf8(value);
        } else if (option == L"--output") {
            const wchar_t* value = next();
            if (value == nullptr) {
                return std::nullopt;
            }
            options.output = value;
        } else {
            options.images.emplace_back(option);
        }
    }
    if (options.output.empty() || options.images.empty()) {
        return std::nullopt;
    }
    return options;
}

std::shared_ptr<const core::FuriganaReadings> loadFurigana(const std::filesystem::path& models) {
    std::ifstream file(models / L"furigana" / L"readings.tsv", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    std::optional<core::FuriganaReadings> readings = core::FuriganaReadings::parse(text);
    return readings ? std::make_shared<const core::FuriganaReadings>(std::move(*readings))
                    : nullptr;
}

int run(const Options& options) {
    const std::filesystem::path models = platform::findModelsDirectory();
    if (models.empty()) {
        std::fputs("找不到 models 資料夾\n", stderr);
        return 1;
    }
    core::Settings settings;
    if (options.settings) {
        settings = platform::loadSettings(*options.settings).settings;
    }
    core::SteadyClock clock;
    const app::TranslationSetup translation = app::makeTranslationService(
        settings, clock,
        core::OpenccConverter::defaultConfig(platform::executableDirectory() / L"opencc"));
    for (const std::string& problem : translation.problems) {
        std::fprintf(stderr, "%s\n", problem.c_str());
    }
    std::string engines;
    for (const std::string& id : translation.engineIds) {
        engines += (engines.empty() ? "" : " → ") + id;
    }
    std::printf("引擎：%s\n", engines.c_str());

    ocr::OcrOptions ocrOptions;
    ocrOptions.warmUpScript = core::languageFromCode(options.language);
    ocrOptions.detection.fixedInput = ocr::lensDetectionInput();  // 和主程式一樣
    ocr::OcrService ocr(models, ocr::Device::Auto, ocrOptions);
    if (options.manga && !ocr.setMangaMode(true)) {
        std::fputs("漫畫模式的模型載入失敗，改用一般模式\n", stderr);
    }
    core::PipelineOptions pipelineOptions;
    pipelineOptions.furigana = loadFurigana(models);
    core::Pipeline pipeline(ocr, *translation.service, pipelineOptions);
    // 背景修補（M4-01）：和主程式一樣，只在 OCR 用顯示卡、而且有模型時
    std::shared_ptr<core::IInpainter> inpainter;
    if (const auto model = ocr::LamaInpainter::modelPath(models);
        ocr.device() == ocr::Device::DirectML && std::filesystem::exists(model)) {
        inpainter = std::make_shared<ocr::LamaInpainter>(model);
        std::printf("背景修補：LaMa\n");
    }
    platform::OverlayRenderer renderer;
    if (const std::string_view file = core::overlayFontFile(options.font); !file.empty()) {
        if (!renderer.setFont(models / L"fonts" / std::filesystem::path(std::string(file)))) {
            std::fprintf(stderr, "載入不了字型 %s，改用微軟正黑體\n", std::string(file).c_str());
        }
    }

    std::filesystem::create_directories(options.output);
    int failures = 0;
    for (const std::filesystem::path& path : options.images) {
        core::PipelineJob job;
        job.frame = platform::loadImage(path);
        job.region = core::RectI{0, 0, job.frame.width, job.frame.height};
        job.language = options.language;
        job.inpainter = inpainter;
        pipeline.forget(job.lens);  // 每張圖各自獨立，不拿上一張當上下文
        const core::PipelineResult result = pipeline.run(job, std::stop_token{});
        std::printf("\n== %s（%zu 段）\n", platform::pathToUtf8(path.filename()).c_str(),
                    result.groups.size());
        if (!result.error.empty()) {
            std::printf("翻譯失敗：%s\n", result.error.c_str());
            ++failures;
        }
        for (const core::TranslatedBlock& group : result.groups) {
            const core::RectI& r = group.block.rect;
            const core::RectI padded{r.left - 3, r.top - 3, r.right + 3, r.bottom + 3};
            const core::Rgba bg = core::sampleBackground(job.frame, padded);
            // 外圍的純度（差 24／72 以內）和 OCR 分數：調整 overlay_plan 的門檻時看這幾個數字
            std::printf(
                "- [%s 純度 %.2f/%.2f 分數 %.2f 位置 %d,%d,%d,%d] %s\n  → %s\n",
                core::worthCovering(job.frame, padded, bg, group.block.score) ? "蓋" : "不蓋",
                core::backgroundUniformity(job.frame, padded, bg),
                core::backgroundUniformity(job.frame, padded, bg, 2, 72), group.block.score, r.left,
                r.top, r.right, r.bottom, group.block.text.c_str(), group.translation.c_str());
        }
        std::printf(
            "耗時：OCR %.0f ms（偵測 %.0f、辨識 %.0f、對話框 %.0f、manga-ocr %.0f）"
            "／分段 %.0f／翻譯 %.0f／覆蓋層 %.0f（含背景修補）／合計 %.0f ms\n",
            result.timings.ocrMs, ocr.lastTimings().detectionMs, ocr.lastTimings().recognitionMs,
            ocr.lastTimings().bubbleMs, result.timings.rereadMs, result.timings.layoutMs,
            result.timings.translationMs, result.timings.overlayMs, result.timings.totalMs());
        for (const core::OverlayItem& item : result.overlay) {
            std::printf(
                "  蓋上：底 %d,%d,%d 字 %d,%d,%d%s  %s\n", item.background.r, item.background.g,
                item.background.b, item.foreground.r, item.foreground.g, item.foreground.b,
                item.outline
                    ? (" 描邊 " + std::to_string(item.outline->r) + "," +
                       std::to_string(item.outline->g) + "," + std::to_string(item.outline->b))
                          .c_str()
                    : "",
                item.text.c_str());
        }
        core::ImageBgra shown = job.frame;
        core::compositeOver(
            shown, renderer.render(core::SizeI{shown.width, shown.height}, result.overlay));
        const std::filesystem::path target =
            options.output / (path.stem().wstring() + L".overlay.png");
        platform::savePng(shown, target);
        std::printf("→ %s\n", platform::pathToUtf8(target).c_str());
    }
    return failures == 0 ? 0 : 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    const std::optional<Options> options = parse(argc, argv);
    if (!options) {
        return usage();
    }
    // WIC（讀寫 PNG）要 COM
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        std::fputs("CoInitializeEx failed\n", stderr);
        return 1;
    }
    try {
        return run(*options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
