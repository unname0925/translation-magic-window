#include "ocr/text_detector.h"

#include <algorithm>
#include <clipper.hpp>
#include <cmath>
#include <cstdlib>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <utility>

namespace tmw::ocr {
namespace {

// PaddleX 的 min_size：最小外接矩形的短邊小於這個值就捨棄（擴張後是 min_size + 2）
constexpr float kMinSize = 3.0f;

// Python 的 round() 是「四捨六入五成雙」，和 std::nearbyint 的預設捨入方式相同
double roundHalfEven(double value) {
    return std::nearbyint(value);
}

// box_score_fast：文字框（多邊形）內機率的平均值
double boxScoreFast(const cv::Mat& probability, const std::array<cv::Point2f, 4>& box) {
    const int h = probability.rows;
    const int w = probability.cols;
    float minX = box[0].x, maxX = box[0].x, minY = box[0].y, maxY = box[0].y;
    for (const cv::Point2f& p : box) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    const int xmin = std::clamp(static_cast<int>(std::floor(minX)), 0, w - 1);
    const int xmax = std::clamp(static_cast<int>(std::ceil(maxX)), 0, w - 1);
    const int ymin = std::clamp(static_cast<int>(std::floor(minY)), 0, h - 1);
    const int ymax = std::clamp(static_cast<int>(std::ceil(maxY)), 0, h - 1);

    cv::Mat mask = cv::Mat::zeros(ymax - ymin + 1, xmax - xmin + 1, CV_8UC1);
    std::vector<cv::Point> polygon;
    polygon.reserve(box.size());
    for (const cv::Point2f& p : box) {
        // numpy 的 astype(np.int32)：往 0 的方向截斷
        polygon.emplace_back(static_cast<int>(p.x - static_cast<float>(xmin)),
                             static_cast<int>(p.y - static_cast<float>(ymin)));
    }
    const std::vector<std::vector<cv::Point>> polygons{polygon};
    cv::fillPoly(mask, polygons, cv::Scalar(1));
    const cv::Rect roi(xmin, ymin, xmax - xmin + 1, ymax - ymin + 1);
    return cv::mean(probability(roi), mask)[0];
}

// unclip：用 Clipper 把文字框往外擴張（和 pyclipper 相同：座標往 0 的方向截斷成整數）
std::vector<cv::Point> unclip(const std::array<cv::Point2f, 4>& box, double unclipRatio) {
    const std::vector<cv::Point2f> contour(box.begin(), box.end());
    const double area = cv::contourArea(contour);
    const double length = cv::arcLength(contour, true);
    const double distance = area * unclipRatio / length;

    ClipperLib::Path path;
    for (const cv::Point2f& p : box) {
        path.emplace_back(static_cast<ClipperLib::cInt>(p.x), static_cast<ClipperLib::cInt>(p.y));
    }
    ClipperLib::ClipperOffset offset;  // 預設 MiterLimit 2.0、ArcTolerance 0.25，和 pyclipper 相同
    offset.AddPath(path, ClipperLib::jtRound, ClipperLib::etClosedPolygon);
    ClipperLib::Paths expanded;
    offset.Execute(expanded, distance);

    // PaddleX：np.array(paths) 再攤平。只有一條路徑，或每條路徑的點數都相同時會全部合併；
    // 點數不同時 numpy 會丟出 ValueError，改用第一條路徑。
    std::vector<cv::Point> points;
    if (expanded.empty()) {
        return points;
    }
    const bool sameSize =
        std::all_of(expanded.begin(), expanded.end(),
                    [&](const ClipperLib::Path& p) { return p.size() == expanded[0].size(); });
    const size_t pathCount = sameSize ? expanded.size() : 1;
    for (size_t i = 0; i < pathCount; ++i) {
        for (const ClipperLib::IntPoint& p : expanded[i]) {
            points.emplace_back(static_cast<int>(p.X), static_cast<int>(p.Y));
        }
    }
    return points;
}

}  // namespace

cv::Size detectionInputSize(int height, int width, const DetectionOptions& options) {
    if (height <= 0 || width <= 0) {
        throw std::invalid_argument("detectionInputSize: empty image");
    }
    const double limit = options.limitSideLen;
    double ratio = 1.0;
    switch (options.limitType) {
        case LimitType::Max:
            if (std::max(height, width) > options.limitSideLen) {
                ratio = limit / (height > width ? height : width);
            }
            break;
        case LimitType::Min:
            if (std::min(height, width) < options.limitSideLen) {
                ratio = limit / (height < width ? height : width);
            }
            break;
        case LimitType::ResizeLong:
            ratio = limit / std::max(height, width);
            break;
    }
    int resizeH = static_cast<int>(height * ratio);
    int resizeW = static_cast<int>(width * ratio);
    if (std::max(resizeH, resizeW) > options.maxSideLimit) {
        const double limitRatio =
            static_cast<double>(options.maxSideLimit) / std::max(resizeH, resizeW);
        resizeH = static_cast<int>(resizeH * limitRatio);
        resizeW = static_cast<int>(resizeW * limitRatio);
    }
    resizeH = std::max(static_cast<int>(roundHalfEven(resizeH / 32.0) * 32), 32);
    resizeW = std::max(static_cast<int>(roundHalfEven(resizeW / 32.0) * 32), 32);
    return {resizeW, resizeH};
}

MiniBox miniBox(const cv::RotatedRect& rect) {
    std::array<cv::Point2f, 4> points;
    rect.points(points.data());
    // Python 的 sorted 是穩定排序：x 相同時保持 boxPoints 的順序
    std::stable_sort(points.begin(), points.end(),
                     [](const cv::Point2f& a, const cv::Point2f& b) { return a.x < b.x; });
    int index1 = 0, index2 = 2, index3 = 3, index4 = 1;
    if (points[1].y > points[0].y) {
        index1 = 0;
        index4 = 1;
    } else {
        index1 = 1;
        index4 = 0;
    }
    if (points[3].y > points[2].y) {
        index2 = 2;
        index3 = 3;
    } else {
        index2 = 3;
        index3 = 2;
    }
    MiniBox box;
    box.points = {points[index1], points[index2], points[index3], points[index4]};
    box.shortSide = std::min(rect.size.width, rect.size.height);
    return box;
}

std::vector<DetectedBox> boxesFromProbability(const cv::Mat& probability, int destWidth,
                                              int destHeight, const DetectionOptions& options) {
    CV_Assert(probability.type() == CV_32FC1);
    // numpy：pred > thresh（float32 和 Python float 比較時，門檻會先轉成 float32）
    cv::Mat bitmap(probability.size(), CV_8UC1);
    for (int y = 0; y < probability.rows; ++y) {
        const float* in = probability.ptr<float>(y);
        std::uint8_t* out = bitmap.ptr<std::uint8_t>(y);
        for (int x = 0; x < probability.cols; ++x) {
            out[x] = in[x] > options.thresh ? 255 : 0;
        }
    }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bitmap, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    // PaddleX：dest_width 是 numpy 的 float64，縮放在 float64 下計算
    const double widthScale = static_cast<double>(destWidth) / probability.cols;
    const double heightScale = static_cast<double>(destHeight) / probability.rows;

    std::vector<DetectedBox> boxes;
    const size_t count = std::min(contours.size(), static_cast<size_t>(options.maxCandidates));
    for (size_t i = 0; i < count; ++i) {
        const MiniBox candidate = miniBox(cv::minAreaRect(contours[i]));
        if (candidate.shortSide < kMinSize) {
            continue;
        }
        const double score = boxScoreFast(probability, candidate.points);
        if (options.boxThresh > score) {
            continue;
        }
        const std::vector<cv::Point> expanded = unclip(candidate.points, options.unclipRatio);
        if (expanded.empty()) {
            continue;
        }
        const MiniBox box = miniBox(cv::minAreaRect(expanded));
        if (box.shortSide < kMinSize + 2) {
            continue;
        }
        DetectedBox detected;
        for (size_t k = 0; k < 4; ++k) {
            const double x = roundHalfEven(static_cast<double>(box.points[k].x) * widthScale);
            const double y = roundHalfEven(static_cast<double>(box.points[k].y) * heightScale);
            detected.points[k] = {static_cast<int>(std::clamp(x, 0.0, double(destWidth))),
                                  static_cast<int>(std::clamp(y, 0.0, double(destHeight)))};
        }
        detected.score = score;
        boxes.push_back(detected);
    }
    return boxes;
}

void sortBoxes(std::vector<DetectedBox>& boxes) {
    // 先依左上角的 (y, x) 排序，再把同一行（y 差距 < 10）的框由左到右調整
    std::stable_sort(boxes.begin(), boxes.end(), [](const DetectedBox& a, const DetectedBox& b) {
        const cv::Point& pa = a.points[0];
        const cv::Point& pb = b.points[0];
        return pa.y != pb.y ? pa.y < pb.y : pa.x < pb.x;
    });
    for (size_t i = 0; i + 1 < boxes.size(); ++i) {
        for (size_t j = i + 1; j-- > 0;) {
            const cv::Point& next = boxes[j + 1].points[0];
            const cv::Point& current = boxes[j].points[0];
            if (std::abs(next.y - current.y) < 10 && next.x < current.x) {
                std::swap(boxes[j], boxes[j + 1]);
            } else {
                break;
            }
        }
    }
}

TextDetector::TextDetector(const std::filesystem::path& modelDir, Device device,
                           const DetectionOptions& options)
    : config_(loadDetectionModelConfig(modelDir / "inference.yml")),
      options_(options),
      model_(modelDir / "inference.onnx", device) {}

cv::Size roundUpToDetectionGrid(cv::Size size) {
    const auto round = [](int value) {
        constexpr int grid = 32;
        return std::max(grid, (value + grid - 1) / grid * grid);
    };
    return {round(size.width), round(size.height)};
}

LetterboxLayout letterboxLayout(cv::Size image, cv::Size fixedInput) {
    if (image.width <= 0 || image.height <= 0 || fixedInput.width <= 0 || fixedInput.height <= 0) {
        throw std::invalid_argument("letterboxLayout: empty size");
    }
    const double scale =
        std::min(fixedInput.width * 1.0 / image.width, fixedInput.height * 1.0 / image.height);
    // 至少 1 個像素，而且不能超過畫布
    const int width =
        std::clamp(static_cast<int>(std::lround(image.width * scale)), 1, fixedInput.width);
    const int height =
        std::clamp(static_cast<int>(std::lround(image.height * scale)), 1, fixedInput.height);
    return {cv::Size(width, height), scale};
}

std::vector<float> TextDetector::preprocess(const cv::Mat& bgr, cv::Size& inputSize) const {
    CV_Assert(bgr.type() == CV_8UC3 && !bgr.empty());
    cv::Mat image = bgr;
    // PaddleX：寬 + 高 < 64 時，先在右下補 0 到至少 32×32
    if (image.rows + image.cols < 64) {
        cv::Mat padded =
            cv::Mat::zeros(std::max(32, image.rows), std::max(32, image.cols), CV_8UC3);
        image.copyTo(padded(cv::Rect(0, 0, image.cols, image.rows)));
        image = padded;
    }
    cv::Mat resized;
    if (options_.fixedInput.width > 0 && options_.fixedInput.height > 0) {
        // 等比例縮放後放在固定大小的畫布左上角，其餘補黑色
        inputSize = roundUpToDetectionGrid(options_.fixedInput);
        const LetterboxLayout layout = letterboxLayout(image.size(), inputSize);
        cv::Mat canvas = cv::Mat::zeros(inputSize, CV_8UC3);
        cv::Mat target = canvas(cv::Rect(0, 0, layout.resized.width, layout.resized.height));
        if (layout.resized == image.size()) {
            image.copyTo(target);
        } else {
            cv::resize(image, target, layout.resized);
        }
        resized = canvas;
    } else {
        inputSize = detectionInputSize(image.rows, image.cols, options_);
        if (inputSize.width == image.cols && inputSize.height == image.rows) {
            resized = image;
        } else {
            cv::resize(image, resized, inputSize);  // 預設 INTER_LINEAR，和 cv2.resize 相同
        }
    }

    // NormalizeImage：先乘 alpha 再加 beta，兩次都在 float32 下計算。
    // 不能合併成 FMA（捨入結果會不同）；MSVC 在 /fp:precise 下預設不會合併。
    std::array<float, 3> alpha{};
    std::array<float, 3> beta{};
    for (size_t c = 0; c < 3; ++c) {
        alpha[c] = static_cast<float>(config_.scale / config_.std[c]);
        beta[c] = static_cast<float>(-config_.mean[c] / config_.std[c]);
    }
    const int h = resized.rows;
    const int w = resized.cols;
    std::vector<float> input(static_cast<size_t>(3) * h * w);
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row = resized.ptr<std::uint8_t>(y);
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 3; ++c) {
                const float value = static_cast<float>(row[x * 3 + c]) * alpha[c];
                input[(static_cast<size_t>(c) * h + y) * w + x] = value + beta[c];
            }
        }
    }
    return input;
}

std::vector<DetectedBox> TextDetector::detect(const cv::Mat& bgr) {
    cv::Size inputSize;
    const std::vector<float> input = preprocess(bgr, inputSize);
    const std::array<std::int64_t, 4> shape{1, 3, inputSize.height, inputSize.width};
    Tensor output = model_.run(input, shape);
    if (output.shape.size() != 4 || output.shape[2] != inputSize.height ||
        output.shape[3] != inputSize.width) {
        throw std::runtime_error("unexpected detection output shape");
    }
    const cv::Mat probability(inputSize.height, inputSize.width, CV_32FC1, output.data.data());
    if (options_.fixedInput.width > 0 && options_.fixedInput.height > 0) {
        // 只看畫面實際佔用的部分，補邊的區域不算（座標換算才會正確）
        const LetterboxLayout layout =
            letterboxLayout(bgr.size(), roundUpToDetectionGrid(options_.fixedInput));
        const cv::Mat used =
            probability(cv::Rect(0, 0, layout.resized.width, layout.resized.height)).clone();
        return boxesFromProbability(used, bgr.cols, bgr.rows, options_);
    }
    return boxesFromProbability(probability, bgr.cols, bgr.rows, options_);
}

}  // namespace tmw::ocr
