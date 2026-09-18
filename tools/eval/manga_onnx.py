"""manga-ocr 的 ONNX 版本：匯出、前處理、解碼（M0-15 可行性驗證）。

manga-ocr（kha-white/manga-ocr-base）是「編碼器（ViT）＋解碼器（BERT）」模型。這裡把它拆成兩個 ONNX：
- encoder.onnx：pixel_values [1, 3, 224, 224] → 編碼結果 [1, 197, 768]
- decoder.onnx：目前的字 input_ids [beams, T] ＋ 編碼結果 → 下一個字的 logits [beams, 6144]
  （沒有 KV cache：每一步都重算整個序列。先確認可行性，C++ 版（M2-03）再考慮加上 cache）

解碼要和官方（transformers 4.57.6 的 generate）完全相同：beam search（4 個候選）、
不可重複的三字組、length_penalty 2.0、early_stopping、最多 300 個字（見 decode_beam）。

必須用 tools/eval/.venv-manga 的 Python 執行。
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parents[2]
MODEL_DIR = REPO_ROOT / "models" / "manga-ocr-base"
ONNX_DIR = REPO_ROOT / "build" / "manga_ocr_onnx"

IMAGE_SIZE = 224
NEG_INF = float("-inf")


@dataclass(frozen=True)
class GenerationSettings:
    """官方模型 config.json 裡的生成參數（transformers 會照這些值執行 generate）。"""

    num_beams: int
    max_length: int
    early_stopping: bool
    no_repeat_ngram_size: int
    length_penalty: float
    decoder_start_token_id: int
    eos_token_id: int
    pad_token_id: int
    vocab_size: int

    @staticmethod
    def load(model_dir: Path = MODEL_DIR) -> "GenerationSettings":
        config = json.loads((model_dir / "config.json").read_text(encoding="utf-8"))
        return GenerationSettings(
            num_beams=config["num_beams"],
            max_length=config["max_length"],
            early_stopping=config["early_stopping"],
            no_repeat_ngram_size=config["no_repeat_ngram_size"],
            length_penalty=config["length_penalty"],
            decoder_start_token_id=config["decoder_start_token_id"],
            eos_token_id=config["eos_token_id"],
            pad_token_id=config["pad_token_id"],
            vocab_size=config["decoder"]["vocab_size"],
        )


# ---------------------------------------------------------------- 匯出


def export(output_dir: Path = ONNX_DIR, opset: int = 17) -> None:
    """把 PyTorch 模型匯出成 encoder.onnx 和 decoder.onnx。"""
    import torch
    from transformers import VisionEncoderDecoderModel

    model = VisionEncoderDecoderModel.from_pretrained(str(MODEL_DIR)).eval()
    output_dir.mkdir(parents=True, exist_ok=True)

    class Encoder(torch.nn.Module):
        def __init__(self, encoder):
            super().__init__()
            self.encoder = encoder

        def forward(self, pixel_values):
            return self.encoder(pixel_values=pixel_values).last_hidden_state

    class Decoder(torch.nn.Module):
        def __init__(self, decoder):
            super().__init__()
            self.decoder = decoder

        def forward(self, input_ids, encoder_hidden_states):
            logits = self.decoder(input_ids=input_ids, encoder_hidden_states=encoder_hidden_states,
                                  use_cache=False).logits
            return logits[:, -1, :]

    # 編碼器和解碼器的大小相同（768），不需要中間的投影層
    assert model.enc_to_dec_proj is None if hasattr(model, "enc_to_dec_proj") else True

    pixel_values = torch.zeros(1, 3, IMAGE_SIZE, IMAGE_SIZE)
    with torch.no_grad():
        torch.onnx.export(Encoder(model.encoder), (pixel_values,), str(output_dir / "encoder.onnx"),
                          input_names=["pixel_values"], output_names=["encoder_hidden_states"],
                          opset_version=opset, dynamo=False)
        hidden = model.encoder(pixel_values=pixel_values).last_hidden_state
        input_ids = torch.tensor([[2, 10, 20], [2, 30, 40]], dtype=torch.int64)
        encoder_hidden = hidden.expand(2, -1, -1).contiguous()
        torch.onnx.export(Decoder(model.decoder), (input_ids, encoder_hidden),
                          str(output_dir / "decoder.onnx"),
                          input_names=["input_ids", "encoder_hidden_states"],
                          output_names=["logits"],
                          dynamic_axes={"input_ids": {0: "beams", 1: "length"},
                                        "encoder_hidden_states": {0: "beams"},
                                        "logits": {0: "beams"}},
                          opset_version=opset, dynamo=False)


# ---------------------------------------------------------------- 前處理


def preprocess(image: Image.Image) -> np.ndarray:
    """和官方相同：轉灰階再轉回 RGB，PIL 雙線性縮成 224×224，(x / 255 - 0.5) / 0.5。"""
    image = image.convert("L").convert("RGB").resize((IMAGE_SIZE, IMAGE_SIZE), Image.BILINEAR)
    # ViTImageProcessor：rescale 先用 float64 乘 1/255 再轉成 float32，normalize 在 float32 下計算
    # （transformers.image_transforms 的 rescale、normalize）。C++ 版要照同樣的順序。
    array = (np.asarray(image, dtype=np.float64) * (1 / 255)).astype(np.float32)
    array = (array - np.float32(0.5)) / np.float32(0.5)
    return array.transpose(2, 0, 1)[None].astype(np.float32)


# ---------------------------------------------------------------- 解碼


def log_softmax(logits: np.ndarray) -> np.ndarray:
    shifted = logits - logits.max(axis=-1, keepdims=True)
    return shifted - np.log(np.exp(shifted).sum(axis=-1, keepdims=True))


def ban_repeated_ngrams(sequences: np.ndarray, log_probs: np.ndarray, n: int) -> None:
    """NoRepeatNGramLogitsProcessor：會讓任何 n 字組第二次出現的字，機率設成 -inf。"""
    length = sequences.shape[1]
    if n <= 0 or length + 1 < n:
        return
    for beam, tokens in enumerate(sequences.tolist()):
        prefix = tuple(tokens[length - n + 1:])
        banned = [tokens[i + n - 1] for i in range(length - n + 1)
                  if tuple(tokens[i:i + n - 1]) == prefix]
        if banned:
            log_probs[beam, banned] = NEG_INF


def top_k(values: np.ndarray, k: int) -> np.ndarray:
    """由大到小的前 k 個的索引（相同時索引小的在前）。"""
    order = np.argsort(-values, kind="stable")
    return order[:k]


def decode_beam(encoder_hidden: np.ndarray, run_decoder, settings: GenerationSettings) -> list[int]:
    """transformers 4.57.6 GenerationMixin._beam_search 的逐步重現（batch size 1）。

    run_decoder(input_ids [beams, T] int64, encoder_hidden [beams, 197, 768]) → logits [beams, V]。
    回傳最佳結果的 token（含開頭的 decoder_start 和結尾的 eos）。
    """
    beams = settings.num_beams
    vocab = settings.vocab_size
    keep = 2 * beams  # beams_to_keep = max(2, 1 + eos 數量) * num_beams
    prompt_len = 1
    cur_len = 1
    penalty = settings.length_penalty

    running = np.full((beams, settings.max_length), settings.pad_token_id, dtype=np.int64)
    running[:, 0] = settings.decoder_start_token_id
    running_scores = np.full(beams, -1e9, dtype=np.float32)
    running_scores[0] = 0.0
    finished = running.copy()
    finished_scores = np.full(beams, -1e9, dtype=np.float32)
    finished_lengths = np.zeros(beams, dtype=np.int64)
    is_finished = np.zeros(beams, dtype=bool)
    improvement_possible = True
    hidden = np.repeat(encoder_hidden, beams, axis=0)

    while True:
        sequences = running[:, :cur_len]
        logits = run_decoder(sequences, hidden).astype(np.float32)
        log_probs = log_softmax(logits)
        ban_repeated_ngrams(sequences, log_probs, settings.no_repeat_ngram_size)
        accumulated = (log_probs + running_scores[:, None]).reshape(-1).astype(np.float32)

        candidates = top_k(accumulated, keep)
        cand_scores = accumulated[candidates]
        cand_beams = candidates // vocab
        cand_tokens = candidates % vocab
        cand_sequences = running[cand_beams].copy()
        cand_sequences[:, cur_len] = cand_tokens
        hits = (cand_tokens == settings.eos_token_id) | (cur_len + 1 >= settings.max_length)

        # 下一輪繼續的候選：已經結束的扣 1e9，取前 num_beams 個
        running_candidates = (cand_scores + hits.astype(np.float32) * np.float32(-1e9)).astype(
            np.float32)
        next_running = top_k(running_candidates, beams)
        running = cand_sequences[next_running]
        running_scores = running_candidates[next_running]

        # 結束的候選：只有前 num_beams 個能算數，分數除以長度的 length_penalty 次方
        just_finished = hits & (np.arange(keep) < beams)
        scored = (cand_scores / np.float32((cur_len + 1 - prompt_len) ** penalty)).astype(np.float32)
        if is_finished.all() and settings.early_stopping:
            scored = scored + np.float32(-1e9)
        if not improvement_possible:
            scored = scored + np.float32(-1e9)
        scored = scored + (~just_finished).astype(np.float32) * np.float32(-1e9)
        merged_scores = np.concatenate([finished_scores, scored]).astype(np.float32)
        merged_sequences = np.concatenate([finished, cand_sequences])
        merged_lengths = np.concatenate([finished_lengths, np.full(keep, cur_len + 1)])
        merged_finished = np.concatenate([is_finished, just_finished])
        best = top_k(merged_scores, beams)
        finished = merged_sequences[best]
        finished_scores = merged_scores[best]
        finished_lengths = merged_lengths[best]
        is_finished = merged_finished[best]

        cur_len += 1
        # _check_early_stop_heuristic（early_stopping=True：用目前長度估計）
        best_running = running_scores[0] / np.float32((cur_len - prompt_len) ** penalty)
        worst_finished = np.where(is_finished, finished_scores.min(), np.float32(-1e9))
        improvement_possible = improvement_possible and bool(np.any(best_running > worst_finished))
        all_finished_early = is_finished.all() and settings.early_stopping
        if not improvement_possible or all_finished_early or hits.all():
            break
    return finished[0, :finished_lengths[0]].tolist()


def decode_greedy(encoder_hidden: np.ndarray, run_decoder, settings: GenerationSettings) -> list[int]:
    """逐字取機率最大的字（用來和 beam search 比較速度和結果）。"""
    tokens = [settings.decoder_start_token_id]
    while len(tokens) < settings.max_length:
        logits = run_decoder(np.array([tokens], dtype=np.int64), encoder_hidden)
        token = int(np.argmax(logits[0]))
        tokens.append(token)
        if token == settings.eos_token_id:
            break
    return tokens


# ---------------------------------------------------------------- 轉成文字


class Detokenizer:
    """BertJapaneseTokenizer.decode(skip_special_tokens=True) 加上 manga-ocr 的 post_process。"""

    SPECIAL = {"[PAD]", "[UNK]", "[CLS]", "[SEP]", "[MASK]"}

    def __init__(self, model_dir: Path = MODEL_DIR):
        self.vocab = (model_dir / "vocab.txt").read_text(encoding="utf-8").splitlines()

    def decode(self, tokens: list[int]) -> str:
        pieces = [self.vocab[t] for t in tokens if self.vocab[t] not in self.SPECIAL]
        text = " ".join(pieces).replace(" ##", "").strip()
        return post_process(text)


def post_process(text: str) -> str:
    """manga_ocr.ocr.post_process 的複製（C++ 版要照著實作）。"""
    import jaconv

    text = "".join(text.split())
    text = text.replace("…", "...")
    text = re.sub("[・.]{2,}", lambda x: (x.end() - x.start()) * ".", text)
    return jaconv.h2z(text, ascii=True, digit=True)


# ---------------------------------------------------------------- ONNX Runtime


class MangaOcrOnnx:
    def __init__(self, device: str = "cpu", onnx_dir: Path = ONNX_DIR):
        import onnxruntime as ort

        options = ort.SessionOptions()
        if device == "dml":
            providers = ["DmlExecutionProvider"]
            options.enable_mem_pattern = False
            options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        else:
            providers = ["CPUExecutionProvider"]
        self.encoder = ort.InferenceSession(str(onnx_dir / "encoder.onnx"), options,
                                            providers=providers)
        self.decoder = ort.InferenceSession(str(onnx_dir / "decoder.onnx"), options,
                                            providers=providers)
        self.settings = GenerationSettings.load()
        self.detokenizer = Detokenizer()

    def encode(self, pixel_values: np.ndarray) -> np.ndarray:
        return self.encoder.run(None, {"pixel_values": pixel_values})[0]

    def run_decoder(self, input_ids: np.ndarray, encoder_hidden: np.ndarray) -> np.ndarray:
        return self.decoder.run(None, {"input_ids": input_ids,
                                       "encoder_hidden_states": encoder_hidden})[0]

    def __call__(self, image: Image.Image, method: str = "beam") -> tuple[str, list[int]]:
        hidden = self.encode(preprocess(image))
        decode = decode_beam if method == "beam" else decode_greedy
        tokens = decode(hidden, self.run_decoder, self.settings)
        return self.detokenizer.decode(tokens), tokens
