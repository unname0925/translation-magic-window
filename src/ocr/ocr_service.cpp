#include "ocr/ocr_service.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <mutex>
#include <opencv2/imgproc.hpp>
#include <utility>

namespace tmw::ocr {

cv::Mat toBgr(const core::ImageBgra& frame) {
    if (frame.empty()) {
        return {};
    }
    const cv::Mat bgra(frame.height, frame.width, CV_8UC4,
                       const_cast<std::uint8_t*>(frame.pixels.data()));
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
}

core::OcrLine toOcrLine(const TextLine& line) {
    int left = line.box[0].x;
    int top = line.box[0].y;
    int right = left;
    int bottom = top;
    for (const cv::Point& point : line.box) {
        left = std::min(left, point.x);
        top = std::min(top, point.y);
        right = std::max(right, point.x);
        bottom = std::max(bottom, point.y);
    }
    core::OcrLine out;
    out.rect = core::RectI{left, top, right, bottom};
    out.text = line.text;
    out.score = line.score;
    // design.md 4.4：高度大於寬度的 1.5 倍就當成直排
    out.orientation = out.rect.height() > out.rect.width() * 3 / 2 ? core::Orientation::Vertical
                                                                   : core::Orientation::Horizontal;
    return out;
}

namespace {

// 沒有硬體顯示卡時直接用 CPU 和 CPU 用的模型（M2-11）。只有 Auto 才需要問 DXGI。
Device resolveForModels(Device requested) {
    return resolveDevice(requested, requested == Device::Auto && firstAdapterIsHardware());
}

}  // namespace

OcrService::OcrService(const std::filesystem::path& modelsDirectory, TextLanguage language,
                       Device device, const OcrOptions& options)
    : modelsDirectory_(modelsDirectory),
      device_(resolveForModels(device)),
      pipeline_(detectionModelPath(modelsDirectory, chooseModels(language, device_)),
                recognitionModelPath(modelsDirectory, chooseModels(language, device_)), device_,
                options) {}

OcrService::OcrService(const std::filesystem::path& modelsDirectory, Device device,
                       const OcrOptions& options)
    // 兩種語言的偵測模型是同一個（M0-11：v6 的偵測對韓文也最好），只有辨識模型不同
    : modelsDirectory_(modelsDirectory),
      device_(resolveForModels(device)),
      pipeline_(detectionModelPath(modelsDirectory,
                                   chooseModels(TextLanguage::JapaneseOrEnglish, device_)),
                recognitionModelPath(modelsDirectory,
                                     chooseModels(TextLanguage::JapaneseOrEnglish, device_)),
                recognitionModelPath(modelsDirectory, chooseModels(TextLanguage::Korean, device_)),
                device_, options) {}

core::OcrResult OcrService::recognize(const core::ImageBgra& frame, core::Language script,
                                      std::stop_token cancel) {
    // acquire 對應 setMangaMode 裡的 release：看到 true 就一定看得到載入好的模型
    return recognizeWith(frame, script, cancel, mangaMode_.load(std::memory_order_acquire));
}

core::OcrResult OcrService::recognizeManga(const core::ImageBgra& frame, core::Language script,
                                           std::stop_token cancel) {
    return recognizeWith(frame, script, cancel, loadMangaModels());
}

core::OcrResult OcrService::recognizeWith(const core::ImageBgra& frame, core::Language script,
                                          std::stop_token cancel, bool bubbles) {
    if (frame.empty() || cancel.stop_requested()) {
        return {};
    }
    const cv::Mat bgr = toBgr(frame);
    const OcrRun run = pipeline_.run(bgr, script, &lastTimings_);
    if (cancel.stop_requested()) {
        return {};
    }
    core::OcrResult out;
    out.script = run.script;
    lastTimings_.bubbleMs = 0.0;
    if (bubbles && !cancel.stop_requested()) {
        const auto bubbleStart = std::chrono::steady_clock::now();
        const std::vector<ComicTextBlock> blocks = comicText_->detect(bgr);
        lastTimings_.bubbleMs = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - bubbleStart)
                                    .count();
        for (const ComicTextBlock& block : blocks) {
            out.bubbles.push_back(block.rect);
        }
    }
    out.lines.reserve(run.lines.size());
    for (const TextLine& line : run.lines) {
        out.lines.push_back(toOcrLine(line));
    }
    return out;
}

std::filesystem::path OcrService::comicTextModelPath(const std::filesystem::path& modelsDirectory) {
    return modelsDirectory / "comic-text-detector" / "comictextdetector.pt.onnx";
}

bool OcrService::setMangaMode(bool enabled) {
    if (!enabled) {
        mangaMode_.store(false, std::memory_order_release);
        return true;
    }
    if (!loadMangaModels()) {
        return false;
    }
    mangaMode_.store(true, std::memory_order_release);
    return true;
}

bool OcrService::loadMangaModels() {
    if (comicTextLoaded_.load(std::memory_order_acquire)) {
        return true;
    }
    {
        const std::lock_guard<std::mutex> lock(comicTextLoading_);
        if (comicText_ == nullptr) {
            std::filesystem::path model = comicTextModelPath(modelsDirectory_);
            if (!std::filesystem::exists(model)) {
                return false;
            }
            // 顯示卡上用 fp16 版（tools/eval/to_fp16.py）：顯示記憶體 796 → 411 MB、推論 39 → 15
            // ms， 測試集 111 個對話框全部對得上。CPU 上 fp16 反而慢，照舊用 fp32
            const std::filesystem::path half = model.parent_path() / "comictextdetector.fp16.onnx";
            if (pipeline_.device() == Device::DirectML && std::filesystem::exists(half)) {
                model = half;
            }
            try {
                comicText_ = std::make_unique<ComicTextDetector>(model, pipeline_.device());
            } catch (const std::exception&) {
                return false;
            }
        }
        // manga-ocr 是加分的：沒有它漫畫模式照樣能依對話框分段，只是文字還是 PP-OCR 讀的
        if (mangaOcr_ == nullptr) {
            const std::filesystem::path directory = mangaOcrDirectory(modelsDirectory_);
            if (std::filesystem::exists(directory / "encoder.onnx")) {
                try {
                    mangaOcr_ = std::make_unique<MangaOcr>(directory, pipeline_.device());
                    mangaOcrLoaded_.store(true, std::memory_order_release);
                } catch (const std::exception&) {
                    mangaOcr_.reset();
                }
            }
        }
    }
    comicTextLoaded_.store(true, std::memory_order_release);
    return true;
}

std::filesystem::path OcrService::mangaOcrDirectory(const std::filesystem::path& modelsDirectory) {
    return modelsDirectory / "manga-ocr";
}

std::vector<std::optional<std::string>> OcrService::reread(
    const core::ImageBgra& frame, std::span<const core::RereadRequest> requests,
    std::stop_token cancel) {
    std::vector<std::optional<std::string>> results(requests.size());
    // acquire 對應載入時的 release：看到 true 就一定看得到載入好的模型
    // 管線只在找到對話框時才重讀（透鏡的漫畫模式或網頁漫畫），所以只看 manga-ocr 有沒有載入
    if (!mangaOcrLoaded_.load(std::memory_order_acquire) || cancel.stop_requested() ||
        frame.empty() || requests.empty()) {
        return results;
    }
    // 四周多留幾個像素：框是依文字行算的，貼得太緊的話字的邊緣會被切掉
    constexpr int kMargin = 4;
    const cv::Mat bgr = toBgr(frame);  // 整張只轉一次
    std::vector<MangaOcrRequest> crops;
    crops.reserve(requests.size());
    for (const core::RereadRequest& request : requests) {
        const core::RectI& rect = request.rect;
        const cv::Rect area = cv::Rect(rect.left - kMargin, rect.top - kMargin,
                                       rect.width() + 2 * kMargin, rect.height() + 2 * kMargin) &
                              cv::Rect(0, 0, bgr.cols, bgr.rows);
        // 空的裁切圖 MangaOcr 會跳過，結果是空字串
        crops.push_back({area.empty() ? cv::Mat() : bgr(area), request.maxCharacters});
    }
    std::vector<std::string> texts = mangaOcr_->read(crops, cancel);
    for (std::size_t i = 0; i < texts.size() && i < results.size(); ++i) {
        if (!texts[i].empty()) {
            results[i] = std::move(texts[i]);
        }
    }
    return results;
}

}  // namespace tmw::ocr
