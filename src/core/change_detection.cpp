#include "core/change_detection.h"

#include <cstdlib>
#include <stdexcept>

namespace tmw::core {

ChangeMetrics measureChange(const GrayImage& before, const GrayImage& after, int pixelDelta) {
    if (before.width != after.width || before.height != after.height) {
        throw std::invalid_argument("measureChange: images have different sizes");
    }
    ChangeMetrics metrics;
    if (before.pixels.empty()) {
        return metrics;
    }
    long long total = 0;
    for (std::size_t i = 0; i < before.pixels.size(); ++i) {
        const int delta = std::abs(static_cast<int>(before.pixels[i]) - after.pixels[i]);
        total += delta;
        if (delta > pixelDelta) {
            ++metrics.changedPixels;
        }
    }
    metrics.meanAbsDelta = static_cast<double>(total) / static_cast<double>(before.pixels.size());
    return metrics;
}

bool contentChanged(const GrayImage& before, const GrayImage& after,
                    const ChangeThresholds& thresholds) {
    if (before.width != after.width || before.height != after.height) {
        return true;
    }
    const ChangeMetrics metrics = measureChange(before, after, thresholds.pixelDelta);
    return metrics.changedPixels >= thresholds.minChangedPixels ||
           metrics.meanAbsDelta > thresholds.meanDelta;
}

GrayImage toGray(const ImageBgra& image) {
    GrayImage gray(image.width, image.height);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::uint8_t* p = image.pixel(x, y);
            // BT.601：0.299 R + 0.587 G + 0.114 B，用整數運算
            gray.at(x, y) =
                static_cast<std::uint8_t>((77 * p[2] + 150 * p[1] + 29 * p[0] + 128) >> 8);
        }
    }
    return gray;
}

}  // namespace tmw::core
