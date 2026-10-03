// 顯示卡上的推論要輪流做：透鏡的處理管線和網頁漫畫的辨識段在不同的執行緒，
// 但共用同一組 OCR 模型（DirectML 的同一個工作階段不能同時被兩個執行緒執行），
// 背景修補（LaMa）也在顯示卡上。用同一把鎖包住它們。
// 翻譯（網路）不在鎖裡：網頁的好幾頁可以一邊翻譯、一邊讓另一頁做 OCR。
#pragma once

#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include "core/inpainter.h"
#include "core/pipeline.h"

namespace tmw::core {

class LockedOcrService final : public IOcrService {
public:
    LockedOcrService(IOcrService& inner, std::mutex& gpu) : inner_(inner), gpu_(gpu) {}

    OcrResult recognize(const ImageBgra& frame, Language script, std::stop_token cancel) override {
        const std::lock_guard lock(gpu_);
        return inner_.recognize(frame, script, cancel);
    }

    OcrResult recognizeManga(const ImageBgra& frame, Language script,
                             std::stop_token cancel) override {
        const std::lock_guard lock(gpu_);
        return inner_.recognizeManga(frame, script, cancel);
    }

    std::vector<std::optional<std::string>> reread(const ImageBgra& frame,
                                                   std::span<const RereadRequest> requests,
                                                   std::stop_token cancel) override {
        const std::lock_guard lock(gpu_);
        return inner_.reread(frame, requests, cancel);
    }

private:
    IOcrService& inner_;
    std::mutex& gpu_;
};

class LockedInpainter final : public IInpainter {
public:
    LockedInpainter(std::shared_ptr<IInpainter> inner, std::mutex& gpu)
        : inner_(std::move(inner)), gpu_(gpu) {}

    std::optional<ImageBgra> inpaint(const ImageBgra& frame, const RectI& rect) override {
        const std::lock_guard lock(gpu_);
        return inner_->inpaint(frame, rect);
    }

private:
    std::shared_ptr<IInpainter> inner_;
    std::mutex& gpu_;
};

}  // namespace tmw::core
