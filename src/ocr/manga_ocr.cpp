#include "ocr/manga_ocr.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <nlohmann/json.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <utility>

#include "core/utf8.h"
#include "ocr/onnx_internal.h"

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

// 顯示卡上用 fp16 版的編碼器（tools/eval/to_fp16.py）：顯示記憶體 417 → 247 MB。CPU 上照舊用 fp32
std::filesystem::path encoderFile(const std::filesystem::path& directory, Device device) {
    const std::filesystem::path half = directory / "encoder.fp16.onnx";
    return device == Device::DirectML && std::filesystem::exists(half) ? half
                                                                       : directory / "encoder.onnx";
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

std::vector<std::vector<std::int64_t>> mangaOcrGreedyDecodeBatch(const MangaOcrBatchStep& step,
                                                                 std::int64_t start,
                                                                 std::int64_t eos,
                                                                 std::span<const int> maxLengths) {
    const std::size_t count = maxLengths.size();
    std::vector<std::vector<std::int64_t>> sequences(count, std::vector<std::int64_t>{start});
    std::vector<bool> done(count);
    std::size_t remaining = 0;
    for (std::size_t i = 0; i < count; ++i) {
        done[i] = maxLengths[i] <= 1;
        remaining += done[i] ? 0 : 1;
    }
    std::vector<std::int64_t> last(count, start);
    for (std::int64_t position = 0; remaining > 0; ++position) {
        const std::span<const float> logits = step(last, position);
        if (logits.empty() || logits.size() % count != 0) {
            break;
        }
        const std::size_t width = logits.size() / count;
        for (std::size_t i = 0; i < count; ++i) {
            if (done[i]) {
                continue;
            }
            // std::max_element 在同分時回傳第一個，和 numpy 的 argmax 一樣
            const auto row = logits.subspan(i * width, width);
            const std::int64_t next = std::max_element(row.begin(), row.end()) - row.begin();
            sequences[i].push_back(next);
            last[i] = next;
            if (next == eos || static_cast<int>(sequences[i].size()) >= maxLengths[i]) {
                done[i] = true;
                --remaining;
            }
        }
    }
    return sequences;
}

// 解碼器的兩個模型（decoder_cross、decoder_step）。直接用 ONNX Runtime 的 IoBinding：
// 圖像特徵的 K／V 和逐步累積的 K／V 都留在裝置上（DirectML 是顯示卡記憶體），
// 每一步只有這一步的字送過去、logits 拿回來。不這樣做的話，一批 8 個區塊每一步要來回搬
// 約 19 MB 的 K／V，比計算本身還慢（量測：每步 6.2 ms 對 3.2 ms）。
struct MangaOcr::Decoder {
    Device device = Device::Cpu;
    Ort::Session cross{nullptr};
    Ort::Session step{nullptr};
    std::int64_t vocabulary = 0;  // logits 每列的長度
    // DirectML：批次固定成 kMangaOcrBatch（不足的補空白）；CPU：有幾個送幾個
    bool fixedBatch = false;

    static constexpr std::array<const char*, 4> kCrossNames{"cross_key_0", "cross_value_0",
                                                            "cross_key_1", "cross_value_1"};
    static constexpr std::array<const char*, 4> kPastNames{"past_key_0", "past_value_0",
                                                           "past_key_1", "past_value_1"};
    static constexpr std::array<const char*, 4> kPresentNames{"present_key_0", "present_value_0",
                                                              "present_key_1", "present_value_1"};

    // actual：編碼器實際用的裝置（DirectML 或 CPU），解碼器跟著用同一個
    Decoder(const std::filesystem::path& directory, Device actual) : device(actual) {
        const std::array<FixedDimension, 2> fixed{FixedDimension{"batch", kMangaOcrBatch},
                                                  FixedDimension{"slots", kMangaOcrSlots}};
        // CPU 沒有「新形狀變慢」的問題，批次不固定，有幾個區塊就送幾個
        const auto dimensions = device == Device::DirectML
                                    ? std::span<const FixedDimension>(fixed)
                                    : std::span<const FixedDimension>(fixed).last(1);
        // 圖形最佳化要開：K／V 的格數固定之後，遮罩和「寫進第幾格」的 arange／where 才會
        // 先算好，不然每一步都要重算（量測：一批 8 個每步 4.7 → 3.2 ms）
        try {
            step = createOnnxSession(directory / "decoder_step.onnx", device, true, dimensions);
            cross = createOnnxSession(directory / "decoder_cross.onnx", device, true, dimensions);
        } catch (const Ort::Exception& error) {
            throw std::runtime_error("cannot load manga-ocr decoder on " +
                                     std::string(deviceName(device)) + ": " + error.what());
        }
        fixedBatch = device == Device::DirectML;

        // 舊格式（K／V 每步變長、批次固定 1）的模型：要重新匯出
        // TensorTypeAndShapeInfo 只是 TypeInfo 裡面的檢視，TypeInfo 要活著
        const Ort::TypeInfo pastType = step.GetInputTypeInfo(2);
        const auto past = pastType.GetTensorTypeAndShapeInfo();
        std::array<const char*, 4> names{};
        const std::vector<std::int64_t> shape = past.GetShape();
        if (shape.size() == 4) {
            past.GetSymbolicDimensions(names.data(), names.size());
        }
        const bool slots =
            shape.size() == 4 && (shape[2] == kMangaOcrSlots ||
                                  (names[2] != nullptr && std::string_view(names[2]) == "slots"));
        if (!slots) {
            throw std::runtime_error(
                "decoder_step.onnx is the old format; re-run tools/eval/export_manga_decoder.py");
        }
        for (std::size_t i = 0; i < step.GetOutputCount(); ++i) {
            Ort::AllocatorWithDefaultOptions allocator;
            if (std::string_view(step.GetOutputNameAllocated(i, allocator).get()) == "logits") {
                vocabulary = step.GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape()[1];
            }
        }
        if (vocabulary <= 0) {
            throw std::runtime_error("decoder_step.onnx has no fixed-size logits output");
        }
    }

    Ort::MemoryInfo deviceMemory() const {
        return device == Device::DirectML
                   ? Ort::MemoryInfo("DML", OrtDeviceAllocator, 0, OrtMemTypeDefault)
                   : Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    }

    // hidden：batch × 197 × 768 的圖像特徵。回傳每一串的字（包含 start）
    std::vector<std::vector<std::int64_t>> decode(std::vector<float>& hidden, std::int64_t batch,
                                                  std::int64_t hiddenTokens,
                                                  std::int64_t hiddenSize, std::int64_t start,
                                                  std::int64_t eos, std::span<const int> limits) {
        const Ort::MemoryInfo cpu =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const Ort::MemoryInfo onDevice = deviceMemory();

        // 圖像特徵的 K／V：一批算一次，留在裝置上
        const std::array<std::int64_t, 3> hiddenShape{batch, hiddenTokens, hiddenSize};
        std::vector<Ort::Value> crossValues;
        {
            Ort::IoBinding binding(cross);
            const Ort::Value input = Ort::Value::CreateTensor<float>(
                cpu, hidden.data(), hidden.size(), hiddenShape.data(), hiddenShape.size());
            binding.BindInput("encoder_hidden_states", input);
            for (const char* name : kCrossNames) {
                binding.BindOutput(name, onDevice);
            }
            cross.Run(Ort::RunOptions{nullptr}, binding);
            binding.SynchronizeOutputs();
            crossValues = binding.GetOutputValues();
        }

        // 第一步的 K／V 從 CPU 上的 0 開始，之後每一步的輸出（在裝置上）直接當下一步的輸入
        const std::array<std::int64_t, 4> pastShape{batch, kHeads, kMangaOcrSlots, kHeadSize};
        std::vector<float> zeros(static_cast<std::size_t>(batch) * kHeads * kMangaOcrSlots *
                                 kHeadSize);
        std::vector<Ort::Value> past;
        for (std::size_t i = 0; i < kPastNames.size(); ++i) {
            past.push_back(Ort::Value::CreateTensor<float>(cpu, zeros.data(), zeros.size(),
                                                           pastShape.data(), pastShape.size()));
        }

        std::vector<std::int64_t> ids(static_cast<std::size_t>(batch), start);
        const std::array<std::int64_t, 2> idsShape{batch, 1};
        std::array<std::int64_t, 1> position{0};
        const std::array<std::int64_t, 1> positionShape{1};
        std::vector<float> logits(static_cast<std::size_t>(batch * vocabulary));
        const std::array<std::int64_t, 2> logitsShape{batch, vocabulary};

        const MangaOcrBatchStep run = [&](std::span<const std::int64_t> tokens,
                                          std::int64_t at) -> std::span<const float> {
            if (at >= kMangaOcrSlots) {
                return {};  // K／V 的格子用完了（maxLengths 已經限制在這之內，不會發生）
            }
            std::copy(tokens.begin(), tokens.end(), ids.begin());
            position[0] = at;
            Ort::IoBinding binding(step);
            const Ort::Value idsValue = Ort::Value::CreateTensor<std::int64_t>(
                cpu, ids.data(), ids.size(), idsShape.data(), idsShape.size());
            const Ort::Value positionValue = Ort::Value::CreateTensor<std::int64_t>(
                cpu, position.data(), position.size(), positionShape.data(), positionShape.size());
            const Ort::Value logitsValue = Ort::Value::CreateTensor<float>(
                cpu, logits.data(), logits.size(), logitsShape.data(), logitsShape.size());
            binding.BindInput("input_ids", idsValue);
            binding.BindInput("position", positionValue);
            for (std::size_t i = 0; i < kPastNames.size(); ++i) {
                binding.BindInput(kPastNames[i], past[i]);
                binding.BindInput(kCrossNames[i], crossValues[i]);
            }
            binding.BindOutput("logits", logitsValue);
            for (const char* name : kPresentNames) {
                binding.BindOutput(name, onDevice);
            }
            step.Run(Ort::RunOptions{nullptr}, binding);
            binding.SynchronizeOutputs();
            std::vector<Ort::Value> outputs = binding.GetOutputValues();
            // 輸出的順序和綁定的順序相同：logits、四個 present
            for (std::size_t i = 0; i < kPastNames.size(); ++i) {
                past[i] = std::move(outputs[i + 1]);
            }
            return logits;
        };
        return mangaOcrGreedyDecodeBatch(run, start, eos, limits);
    }
};

MangaOcr::MangaOcr(const std::filesystem::path& directory, Device device)
    // 編碼器要開 ONNX Runtime 的圖形最佳化：DirectML 上每張 13 → 5.3 ms（124 個區塊的平均），
    // 代價是漫畫模式的工作集多約 270 MB（1203 → 1479 MB）。先存好最佳化過的模型再關掉最佳化載入
    // 也一樣多（1460 MB）：多的是 DirectML 編譯融合後的圖，不是最佳化的過程。
    // 以前量到「幾乎不變」是因為那時候時間都花在逐字解碼上
    : encoder_(encoderFile(directory, device), device, {}, /*optimizeGraph=*/true),
      vocab_(readVocab(directory / "vocab.txt")) {
    // 解碼器跟著編碼器實際用的裝置
    decoder_ = std::make_unique<Decoder>(directory, encoder_.device());
    std::ifstream in(directory / "config.json", std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read manga-ocr config.json");
    }
    const nlohmann::json config = nlohmann::json::parse(in);
    startToken_ = config.at("decoder_start_token_id").get<std::int64_t>();
    endToken_ = config.at("eos_token_id").get<std::int64_t>();
    maxLength_ = config.at("max_length").get<int>();
}

MangaOcr::~MangaOcr() = default;

std::string MangaOcr::read(const cv::Mat& bgr, int maxCharacters) {
    const std::array<MangaOcrRequest, 1> request{MangaOcrRequest{bgr, maxCharacters}};
    return read(request).front();
}

std::vector<std::string> MangaOcr::read(std::span<const MangaOcrRequest> requests,
                                        std::stop_token cancel) {
    std::vector<std::string> results(requests.size());
    // 同一步走：一批的步數是裡面最長的那個，所以字數上限相近的放在同一批
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        if (!requests[i].bgr.empty()) {
            order.push_back(i);
        }
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return requests[a].maxCharacters < requests[b].maxCharacters;
    });
    for (std::size_t first = 0; first < order.size(); first += kMangaOcrBatch) {
        if (cancel.stop_requested()) {
            break;
        }
        const std::size_t count = std::min<std::size_t>(kMangaOcrBatch, order.size() - first);
        readBatch(requests, std::span(order).subspan(first, count), results);
    }
    return results;
}

void MangaOcr::readBatch(std::span<const MangaOcrRequest> requests,
                         std::span<const std::size_t> items, std::vector<std::string>& results) {
    const std::int64_t batch =
        decoder_->fixedBatch ? kMangaOcrBatch : static_cast<std::int64_t>(items.size());
    std::vector<float> hidden;
    std::array<std::int64_t, 3> hiddenShape{};
    const std::array<std::int64_t, 4> pixelShape{1, 3, kImageSize, kImageSize};
    // 編碼器一次一張：批次加大它幾乎沒有變快（每張約 6 ms），還要多一份編碼器的記憶體
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Tensor encoded = encoder_.run(preprocess(requests[items[i]].bgr), pixelShape);
        if (encoded.shape.size() != 3 || encoded.shape[0] != 1) {
            throw std::runtime_error("unexpected manga-ocr encoder output");
        }
        if (hidden.empty()) {
            hiddenShape = {batch, encoded.shape[1], encoded.shape[2]};
            hidden.assign(static_cast<std::size_t>(batch) * encoded.data.size(), 0.0f);
        }
        std::copy(encoded.data.begin(), encoded.data.end(),
                  hidden.begin() + static_cast<std::ptrdiff_t>(i * encoded.data.size()));
    }

    // 總長度包含開頭的 [CLS] 和結尾的 [SEP]；補空白的位置上限 1（一開始就結束）
    const int longest = std::min(maxLength_, kMangaOcrSlots + 1);
    std::vector<int> limits(static_cast<std::size_t>(batch), 1);
    for (std::size_t i = 0; i < items.size(); ++i) {
        limits[i] = std::clamp(requests[items[i]].maxCharacters + 2, 2, longest);
    }
    try {
        const std::vector<std::vector<std::int64_t>> sequences = decoder_->decode(
            hidden, batch, hiddenShape[1], hiddenShape[2], startToken_, endToken_, limits);
        for (std::size_t i = 0; i < items.size(); ++i) {
            results[items[i]] = mangaOcrDetokenize(sequences[i], vocab_);
        }
    } catch (const Ort::Exception& error) {
        throw std::runtime_error(std::string("manga-ocr decoding failed: ") + error.what());
    }
}

}  // namespace tmw::ocr
