#include "ocr/manga_ocr.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <nlohmann/json.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <utility>

#include "core/utf8.h"

namespace tmw::ocr {
namespace {

constexpr int kHeads = 12;
constexpr int kHeadSize = 64;
constexpr std::array<std::string_view, 5> kSpecialTokens = {"[PAD]", "[UNK]", "[CLS]", "[SEP]",
                                                            "[MASK]"};

// Python 的 str.split() 認得的空白
bool isPythonWhitespace(char32_t c) {
    return (c >= 0x09 && c <= 0x0D) || c == 0x20 || (c >= 0x1C && c <= 0x1F) || c == 0x85 ||
           c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
           c == 0x202F || c == 0x205F || c == 0x3000;
}

constexpr char32_t kMiddleDot = U'・';

std::vector<std::string> readVocab(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read manga-ocr vocab");
    }
    std::vector<std::string> vocab;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        vocab.push_back(line);
    }
    return vocab;
}

// 灰階、縮成 224×224、(x/255 − 0.5)/0.5，排成 1×3×224×224（三個通道一樣）。
// 參考實作用 PIL 的雙線性，它是「先水平、再垂直」分兩次縮放，縮小的那一軸會先平滑；
// 這裡也分兩次，縮小的那一軸用 INTER_AREA、放大的用 INTER_LINEAR 來接近它。
std::vector<float> preprocess(const cv::Mat& bgr) {
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    const int size = MangaOcr::kImageSize;
    cv::Mat wide;
    cv::resize(gray, wide, cv::Size(size, gray.rows), 0, 0,
               size < gray.cols ? cv::INTER_AREA : cv::INTER_LINEAR);
    cv::Mat square;
    cv::resize(wide, square, cv::Size(size, size), 0, 0,
               size < wide.rows ? cv::INTER_AREA : cv::INTER_LINEAR);

    const std::size_t plane = static_cast<std::size_t>(size) * size;
    std::vector<float> input(plane * 3);
    for (int y = 0; y < size; ++y) {
        const std::uint8_t* row = square.ptr<std::uint8_t>(y);
        for (int x = 0; x < size; ++x) {
            // 參考實作：先用 float64 乘 1/255 再轉 float32，normalize 在 float32 下計算
            const float value = static_cast<float>(row[x] * (1.0 / 255.0));
            const float normalized = (value - 0.5f) / 0.5f;
            const std::size_t index = static_cast<std::size_t>(y) * size + x;
            input[index] = normalized;
            input[plane + index] = normalized;
            input[plane * 2 + index] = normalized;
        }
    }
    return input;
}

}  // namespace

std::string mangaOcrPostProcess(std::string_view text) {
    // 1. 去掉空白；2. 「…」換成「...」
    std::u32string characters;
    for (std::size_t index = 0; index < text.size();) {
        const char32_t c = core::nextCodePoint(text, index);
        if (isPythonWhitespace(c)) {
            continue;
        }
        if (c == U'…') {
            characters += U"...";
        } else {
            characters += c;
        }
    }
    // 3. 兩個以上連續的「・」或「.」換成同樣數量的「.」
    for (std::size_t start = 0; start < characters.size();) {
        if (characters[start] != kMiddleDot && characters[start] != U'.') {
            ++start;
            continue;
        }
        std::size_t end = start;
        while (end < characters.size() &&
               (characters[end] == kMiddleDot || characters[end] == U'.')) {
            ++end;
        }
        if (end - start >= 2) {
            std::fill(characters.begin() + static_cast<std::ptrdiff_t>(start),
                      characters.begin() + static_cast<std::ptrdiff_t>(end), U'.');
        }
        start = end;
    }
    // 4. 半形英數和符號換成全形
    std::string out;
    out.reserve(text.size() * 2);
    for (char32_t c : characters) {
        core::appendCodePoint(out, c >= 0x21 && c <= 0x7E ? c + 0xFEE0 : c);
    }
    return out;
}

std::string mangaOcrDetokenize(std::span<const std::int64_t> tokens,
                               std::span<const std::string> vocab) {
    std::string joined;
    for (const std::int64_t token : tokens) {
        if (token < 0 || static_cast<std::size_t>(token) >= vocab.size()) {
            continue;
        }
        const std::string& piece = vocab[static_cast<std::size_t>(token)];
        if (std::find(kSpecialTokens.begin(), kSpecialTokens.end(), piece) !=
            kSpecialTokens.end()) {
            continue;
        }
        // 參考實作用空白接起來、再把「 ##」拿掉；這個詞表是單字級的，沒有 ## 開頭的詞，
        // 而空白在 post_process 會全部去掉，所以直接接起來結果相同
        joined += piece;
    }
    return mangaOcrPostProcess(joined);
}

std::vector<std::int64_t> mangaOcrGreedyDecode(const MangaOcrStep& step, std::int64_t start,
                                               std::int64_t eos, int maxLength) {
    std::vector<std::int64_t> tokens{start};
    while (static_cast<int>(tokens.size()) < maxLength) {
        const std::span<const float> logits =
            step(tokens.back(), static_cast<std::int64_t>(tokens.size()) - 1);
        if (logits.empty()) {
            break;
        }
        // std::max_element 在同分時回傳第一個，和 numpy 的 argmax 一樣
        const std::int64_t next = std::max_element(logits.begin(), logits.end()) - logits.begin();
        tokens.push_back(next);
        if (next == eos) {
            break;
        }
    }
    return tokens;
}

MangaOcr::MangaOcr(const std::filesystem::path& directory, Device device)
    : encoder_(directory / "encoder.onnx", device),
      cross_(directory / "decoder_cross.onnx", device),
      step_(directory / "decoder_step.onnx", device),
      vocab_(readVocab(directory / "vocab.txt")) {
    std::ifstream in(directory / "config.json", std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read manga-ocr config.json");
    }
    const nlohmann::json config = nlohmann::json::parse(in);
    startToken_ = config.at("decoder_start_token_id").get<std::int64_t>();
    endToken_ = config.at("eos_token_id").get<std::int64_t>();
    maxLength_ = config.at("max_length").get<int>();
}

std::string MangaOcr::read(const cv::Mat& bgr, int maxCharacters) {
    if (bgr.empty()) {
        return {};
    }
    const std::vector<float> pixels = preprocess(bgr);
    const std::array<std::int64_t, 4> pixelShape{1, 3, kImageSize, kImageSize};
    const Tensor hidden = encoder_.run(pixels, pixelShape);

    // 圖像特徵的 K／V：整個區塊算一次
    const std::array<const char*, 1> crossInput{"encoder_hidden_states"};
    const std::array<const char*, 4> crossOutputs{"cross_key_0", "cross_value_0", "cross_key_1",
                                                  "cross_value_1"};
    const std::array<NamedInput, 1> crossInputs{
        NamedInput::ofFloats(crossInput[0], hidden.shape, hidden.data)};
    const std::vector<Tensor> cross = cross_.run(crossInputs, crossOutputs);

    // 前面累積的 self-attention K／V，第一步是空的
    std::array<Tensor, 4> past;
    for (Tensor& tensor : past) {
        tensor.shape = {1, kHeads, 0, kHeadSize};
    }
    const std::array<const char*, 5> stepOutputs{"logits", "present_key_0", "present_value_0",
                                                 "present_key_1", "present_value_1"};
    Tensor logits;
    const auto step = [&](std::int64_t token, std::int64_t position) -> std::span<const float> {
        const std::array<std::int64_t, 1> tokenData{token};
        const std::array<std::int64_t, 2> tokenShape{1, 1};
        const std::array<std::int64_t, 1> positionData{position};
        const std::array<std::int64_t, 1> positionShape{1};
        const std::array<NamedInput, 10> inputs{
            NamedInput::ofIntegers("input_ids", tokenShape, tokenData),
            NamedInput::ofIntegers("position", positionShape, positionData),
            NamedInput::ofFloats("past_key_0", past[0].shape, past[0].data),
            NamedInput::ofFloats("past_value_0", past[1].shape, past[1].data),
            NamedInput::ofFloats("past_key_1", past[2].shape, past[2].data),
            NamedInput::ofFloats("past_value_1", past[3].shape, past[3].data),
            NamedInput::ofFloats("cross_key_0", cross[0].shape, cross[0].data),
            NamedInput::ofFloats("cross_value_0", cross[1].shape, cross[1].data),
            NamedInput::ofFloats("cross_key_1", cross[2].shape, cross[2].data),
            NamedInput::ofFloats("cross_value_1", cross[3].shape, cross[3].data)};
        std::vector<Tensor> outputs = step_.run(inputs, stepOutputs);
        logits = std::move(outputs[0]);
        for (std::size_t i = 0; i < past.size(); ++i) {
            past[i] = std::move(outputs[i + 1]);
        }
        return logits.data;
    };

    // 總長度包含開頭的 [CLS] 和結尾的 [SEP]
    const int limit = std::clamp(maxCharacters + 2, 2, maxLength_);
    const std::vector<std::int64_t> tokens =
        mangaOcrGreedyDecode(step, startToken_, endToken_, limit);
    return mangaOcrDetokenize(tokens, vocab_);
}

}  // namespace tmw::ocr
