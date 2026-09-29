// 日文漫畫直排對白的辨識：manga-ocr（kha-white/manga-ocr-base），M2-03。
//
// 在漫畫模式分出來的區塊上實測（tools/eval/evaluate_manga_blocks.py，68 個直排對白、1035 字），
// 字元錯誤率 PP-OCR 10.4%、manga-ocr 5.0%，一字不差的區塊 56% 對 76%。
//
// 「編碼器（ViT）＋解碼器（2 層 BERT）」，移植自 tools/eval/manga_onnx.py 和
// tools/eval/export_manga_decoder.py：
// - 前處理：灰階、縮成 224×224、(x/255 − 0.5)/0.5
// - 逐字解碼（M0-11：比官方的 beam search 準），解碼器有 KV cache：
//   decoder_cross.onnx 每個區塊算一次圖像特徵的 K／V，decoder_step.onnx 每次只處理一個新字。
//   DirectML 上每個區塊從 197 ms 降到 19 ms（export_manga_decoder.py 的量測）。
// - 轉回文字：略過 5 個特殊符號（[PAD] [UNK] [CLS] [SEP] [MASK]），其餘直接接起來
// - 後處理：manga_ocr 的 post_process（去空白、刪節號換成點、半形英數轉全形）
//
// 模型檔放在同一個資料夾：encoder.onnx、decoder_cross.onnx、decoder_step.onnx、vocab.txt、config.json。
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <opencv2/core.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ocr/onnx_model.h"
#include "ocr/onnx_session.h"

namespace tmw::ocr {

// manga_ocr 的 post_process：
// 1. 去掉所有空白（Python 的 str.split() 認得的空白，包含全形空白）
// 2. 「…」換成「...」
// 3. 兩個以上連續的「・」或「.」換成同樣數量的「.」
// 4. 半形英數和符號（U+0021～U+007E）換成全形（jaconv.h2z(ascii=True, digit=True)）
std::string mangaOcrPostProcess(std::string_view text);

// 解碼出來的字（包含開頭的 [CLS] 和結尾的 [SEP]）轉成文字，再做 post_process。
// vocab：vocab.txt 的每一行。超出詞表的編號略過。
std::string mangaOcrDetokenize(std::span<const std::int64_t> tokens,
                               std::span<const std::string> vocab);

// 逐字解碼：每一步把上一個字和它的位置交給 step，取 logits 最大的那個字（同分取編號小的），
// 直到產生 eos 或總長度（含開頭的 start）達到 maxLength。回傳包含 start 的整串字。
// step 回傳的 logits 要在下一次呼叫 step 之前都有效。
using MangaOcrStep =
    std::function<std::span<const float>(std::int64_t token, std::int64_t position)>;
std::vector<std::int64_t> mangaOcrGreedyDecode(const MangaOcrStep& step, std::int64_t start,
                                               std::int64_t eos, int maxLength);

class MangaOcr {
public:
    static constexpr int kImageSize = 224;

    // directory：放模型檔的資料夾（見檔頭）。建立失敗時丟出 std::runtime_error。
    MangaOcr(const std::filesystem::path& directory, Device device);

    // bgr：一個對話框的裁切圖（CV_8UC3）。
    // maxCharacters：最多產生幾個字。逐字解碼偶爾會一直重複同一個字停不下來
    // （M0-11：一整行極細長的註解重複了 98 個字），呼叫端依區塊大小給一個上限。
    std::string read(const cv::Mat& bgr, int maxCharacters);

    Device device() const { return encoder_.device(); }

private:
    OnnxModel encoder_;
    OnnxSession cross_;
    OnnxSession step_;
    std::vector<std::string> vocab_;
    std::int64_t startToken_ = 2;
    std::int64_t endToken_ = 3;
    int maxLength_ = 300;
};

}  // namespace tmw::ocr
