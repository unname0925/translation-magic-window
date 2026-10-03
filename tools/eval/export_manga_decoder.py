"""M2-03：把 manga-ocr 的解碼器匯出成「有 KV cache」的兩個 ONNX，並確認結果和沒有 cache 的版本相同。

量測（evaluate_manga_blocks.py 的區塊，中位數 17 步）：DirectML 上編碼器 10 ms，解碼卻要 197 ms
（每步 11.6 ms）。解碼器只有 2 層，慢在沒有 cache 時每一步都把整段重算一次——包括對 197 個圖像特徵
的 cross-attention 投影（每步約 0.5 GFLOP），而那部分第一步算完就不會再變。

所以拆成兩個模型：
- decoder_cross.onnx：encoder_hidden_states [B, 197, 768] → 兩層 cross-attention 的 K、V
  （各 [B, 12, 197, 64]），整個區塊只跑一次
- decoder_step.onnx：這一步的字 input_ids [B, 1]、它的位置 position [1]、前面累積的 self-attention
  K、V（各 [B, 12, S, 64]）、cross 的 K、V → 下一個字的 logits [B, 6144] 和更新後的
  self-attention K、V（各 [B, 12, S, 64]）

B（維度名稱 batch）是一次讀幾個區塊：整頁的直排對白一起逐字解碼，所有區塊同一步走，
每一步的固定開銷只付一次。S（slots）是 K、V 的格數，固定不變：第 position 步把新的 K、V
寫進第 position 格，self-attention 只看第 0～position 格（其餘加上 -1e9）。形狀固定，DirectML
才會走快的路線（形狀每一步都變的話，每一步都是新形狀），而且 K、V 可以一直留在顯示卡上
（C++ 用 IoBinding，見 src/ocr/manga_ocr.cpp），不用每一步搬回 CPU。

量測（DirectML、RTX 4070、24 步，docs/proposal-speed-and-web-manga.md）：
- 舊版（批次 1、K、V 每步變長）：一個區塊約 62 ms
- 批次 8、S=64、K、V 留在顯示卡：8 個區塊共 83 ms（每個 10 ms）；批次 12：每個 9 ms

單步的計算直接用模型的權重自己寫（標準的 BERT 層），不依賴 transformers 在各版本之間一直改的
cache 介面。驗證分兩層：
1. 對同一串字，單步模組每一步的 logits 和原模型（整段一次算）逐步比對
2. 匯出後，在真實裁切圖上用 ONNX Runtime 逐字解碼，產生的字要和沒有 cache 的 decoder.onnx 完全相同

    tools/eval/.venv-manga/Scripts/python tools/eval/export_manga_decoder.py \\
        --crops-from build/ocr_eval/m2-02/with_ctd_fixed.json --images testdata/private/ja-manga
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

from manga_onnx import MODEL_DIR, ONNX_DIR, MangaOcrOnnx, decode_greedy, preprocess

HEADS = 12
HEAD_SIZE = 64
# K、V 的格數（C++ 的 kMangaOcrSlots 要相同）：開頭的 [CLS] 加 95 個字。
# 量測的 124 個直排對白最長 38 字
SLOTS = 96
BATCH = 8  # 一批幾個區塊，和 C++ 的 kMangaOcrBatch 相同


def build_modules():
    import torch
    from torch import nn
    from transformers import VisionEncoderDecoderModel

    model = VisionEncoderDecoderModel.from_pretrained(str(MODEL_DIR)).eval()
    # 底下兩個模組透過閉包用權重，沒有把它們註冊成參數；匯出時權重會被當成常數寫進 ONNX，
    # 常數不能帶著梯度
    model.requires_grad_(False)
    decoder = model.decoder  # BertForMaskedLM（is_decoder、add_cross_attention）
    bert = decoder.bert
    layers = bert.encoder.layer

    def split(x):  # [B, T, 768] → [B, 12, T, 64]
        return x.view(x.shape[0], x.shape[1], HEADS, HEAD_SIZE).permute(0, 2, 1, 3)

    def merge(x):  # [B, 12, T, 64] → [B, T, 768]
        return x.permute(0, 2, 1, 3).reshape(x.shape[0], x.shape[2], HEADS * HEAD_SIZE)

    def attend(q, k, v, mask=None):
        scores = torch.matmul(q, k.transpose(-1, -2)) / math.sqrt(HEAD_SIZE)
        if mask is not None:
            scores = scores + mask
        return torch.matmul(torch.softmax(scores, dim=-1), v)

    class Cross(nn.Module):
        def forward(self, encoder_hidden_states):
            outputs = []
            for layer in layers:
                attention = layer.crossattention.self
                outputs.append(split(attention.key(encoder_hidden_states)))
                outputs.append(split(attention.value(encoder_hidden_states)))
            return tuple(outputs)

    class Step(nn.Module):
        def forward(self, input_ids, position, past_key_0, past_value_0, past_key_1, past_value_1,
                    cross_key_0, cross_value_0, cross_key_1, cross_value_1):
            embeddings = bert.embeddings
            hidden = (embeddings.word_embeddings(input_ids)
                      + embeddings.position_embeddings(position).unsqueeze(0)
                      + embeddings.token_type_embeddings(torch.zeros_like(input_ids)))
            hidden = embeddings.LayerNorm(hidden)

            # 第 position 格放這一步的 K、V；只看得到第 0～position 格（等同於因果遮罩）
            slots = torch.arange(past_key_0.shape[2], dtype=torch.int64)
            here = (slots == position).to(torch.float32).view(1, 1, -1, 1)
            mask = torch.where(slots <= position, 0.0, -1e9).to(torch.float32).view(1, 1, 1, -1)

            pasts = ((past_key_0, past_value_0), (past_key_1, past_value_1))
            crosses = ((cross_key_0, cross_value_0), (cross_key_1, cross_value_1))
            presents = []
            for layer, (past_key, past_value), (cross_key, cross_value) in zip(layers, pasts,
                                                                                 crosses):
                attention = layer.attention.self
                key = past_key * (1 - here) + split(attention.key(hidden)) * here
                value = past_value * (1 - here) + split(attention.value(hidden)) * here
                presents += [key, value]
                context = merge(attend(split(attention.query(hidden)), key, value, mask))
                output = layer.attention.output
                hidden = output.LayerNorm(output.dense(context) + hidden)

                # cross-attention：看圖像特徵，K、V 第一步就算好了
                cross = layer.crossattention
                context = merge(attend(split(cross.self.query(hidden)), cross_key, cross_value))
                hidden = cross.output.LayerNorm(cross.output.dense(context) + hidden)

                # 前饋
                intermediate = layer.intermediate.intermediate_act_fn(
                    layer.intermediate.dense(hidden))
                hidden = layer.output.LayerNorm(layer.output.dense(intermediate) + hidden)

            logits = decoder.cls(hidden)[:, -1, :]
            return (logits, *presents)

    return model, Cross().eval(), Step().eval()


def check_against_model(model, cross, step) -> float:
    """兩個區塊一起：單步模組每一步的 logits 和原模型（整段一次算）比對，回傳最大差距。"""
    import torch

    torch.manual_seed(0)
    pixel_values = torch.rand(2, 3, 224, 224) * 2 - 1
    tokens = torch.tensor([[2, 150, 38, 2011, 76, 900, 5], [2, 7, 99, 300, 12, 13, 14]])
    with torch.no_grad():
        hidden = model.encoder(pixel_values=pixel_values).last_hidden_state
        full = model.decoder(input_ids=tokens, encoder_hidden_states=hidden,
                             use_cache=False).logits
        cross_kv = cross(hidden)
        past = [torch.zeros(2, HEADS, SLOTS, HEAD_SIZE) for _ in range(4)]
        worst = 0.0
        for position in range(tokens.shape[1]):
            outputs = step(tokens[:, position:position + 1], torch.tensor([position]), *past,
                           *cross_kv)
            worst = max(worst, float((outputs[0] - full[:, position]).abs().max()))
            past = list(outputs[1:])
    return worst


def export(cross, step, output_dir: Path) -> None:
    import torch

    hidden = torch.zeros(2, 197, 768)
    cross_names = ["cross_key_0", "cross_value_0", "cross_key_1", "cross_value_1"]
    with torch.no_grad():
        torch.onnx.export(cross, (hidden,), str(output_dir / "decoder_cross.onnx"),
                          input_names=["encoder_hidden_states"], output_names=cross_names,
                          dynamic_axes={"encoder_hidden_states": {0: "batch"},
                                        **{name: {0: "batch"} for name in cross_names}},
                          opset_version=17, dynamo=False)
        past = [torch.zeros(2, HEADS, SLOTS, HEAD_SIZE) for _ in range(4)]
        cross_kv = cross(hidden)
        past_names = ["past_key_0", "past_value_0", "past_key_1", "past_value_1"]
        present_names = ["present_key_0", "present_value_0", "present_key_1", "present_value_1"]
        torch.onnx.export(
            step, (torch.tensor([[5], [6]]), torch.tensor([3]), *past, *cross_kv),
            str(output_dir / "decoder_step.onnx"),
            input_names=["input_ids", "position", *past_names, *cross_names],
            output_names=["logits", *present_names],
            dynamic_axes={"input_ids": {0: "batch"}, "logits": {0: "batch"},
                          **{name: {0: "batch", 2: "slots"} for name in past_names},
                          **{name: {0: "batch", 2: "slots"} for name in present_names},
                          **{name: {0: "batch"} for name in cross_names}},
            opset_version=17, dynamo=False)


class CachedDecoder:
    """用 decoder_cross.onnx 和 decoder_step.onnx 一次解碼好幾個區塊（C++ 版要照著做）。"""

    def __init__(self, device: str, onnx_dir: Path = ONNX_DIR):
        import onnxruntime as ort

        options = ort.SessionOptions()
        options.add_free_dimension_override_by_name("slots", SLOTS)
        if device == "dml":
            providers = ["DmlExecutionProvider"]
            options.enable_mem_pattern = False
            options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        else:
            providers = ["CPUExecutionProvider"]
        self.cross = ort.InferenceSession(str(onnx_dir / "decoder_cross.onnx"), options,
                                          providers=providers)
        self.step = ort.InferenceSession(str(onnx_dir / "decoder_step.onnx"), options,
                                         providers=providers)

    def greedy(self, encoder_hidden: np.ndarray, settings) -> list[list[int]]:
        """encoder_hidden [B, 197, 768]：B 個區塊一起逐字解碼，每個區塊各自停在 eos。"""
        batch = encoder_hidden.shape[0]
        cross = self.cross.run(None, {"encoder_hidden_states": encoder_hidden})
        past = [np.zeros((batch, HEADS, SLOTS, HEAD_SIZE), dtype=np.float32) for _ in range(4)]
        tokens = [[settings.decoder_start_token_id] for _ in range(batch)]
        done = [False] * batch
        limit = min(settings.max_length, SLOTS + 1)
        for position in range(limit - 1):
            outputs = self.step.run(None, {
                "input_ids": np.array([[t[-1]] for t in tokens], dtype=np.int64),
                "position": np.array([position], dtype=np.int64),
                "past_key_0": past[0], "past_value_0": past[1],
                "past_key_1": past[2], "past_value_1": past[3],
                "cross_key_0": cross[0], "cross_value_0": cross[1],
                "cross_key_1": cross[2], "cross_value_1": cross[3]})
            past = outputs[1:]
            for i in range(batch):
                if not done[i]:
                    tokens[i].append(int(np.argmax(outputs[0][i])))
                    done[i] = tokens[i][-1] == settings.eos_token_id
            if all(done):
                break
        return tokens


INSTALL_DIR = MODEL_DIR.parent / "manga-ocr"


def install() -> int:
    """產品（ocr/manga_ocr）要的檔案集中到 models/manga-ocr：三個 ONNX、詞表、設定。"""
    import shutil

    INSTALL_DIR.mkdir(parents=True, exist_ok=True)
    for name in ("encoder.onnx", "decoder_cross.onnx", "decoder_step.onnx"):
        source = ONNX_DIR / name
        if not source.exists():
            print(f"找不到 {source}：先執行 manga_onnx.export() 和這個腳本（不加 --install）")
            return 1
        shutil.copy2(source, INSTALL_DIR / name)
    for name in ("vocab.txt", "config.json"):
        shutil.copy2(MODEL_DIR / name, INSTALL_DIR / name)
    print(f"已放到 {INSTALL_DIR}")
    return 0


def crops_from(ocr: Path, images: Path, limit: int) -> list[Image.Image]:
    crops = []
    for image in json.loads(ocr.read_text(encoding="utf-8"))["images"]:
        with Image.open(images / image["image"]) as opened:
            page = opened.convert("RGB")
        for block in image["blocks"]:
            if block["vertical"] and len(crops) < limit:
                left, top, right, bottom = block["rect"]
                crops.append(page.crop((max(0, left - 4), max(0, top - 4),
                                        min(page.width, right + 4),
                                        min(page.height, bottom + 4))))
    return crops


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--crops-from", required=True, help="ocr_cli 的結果（漫畫模式）")
    parser.add_argument("--images", required=True)
    parser.add_argument("--limit", type=int, default=40)
    parser.add_argument("--install", action="store_true",
                        help="只把產品需要的檔案集中到 models/manga-ocr（不重新匯出）")
    args = parser.parse_args()

    if args.install:
        return install()

    model, cross, step = build_modules()
    worst = check_against_model(model, cross, step)
    print(f"1. 單步模組 vs 原模型：logits 最大差距 {worst:.2e}")
    if worst > 1e-3:
        print("   差太多，不匯出")
        return 1
    export(cross, step, ONNX_DIR)
    print(f"   已匯出 decoder_cross.onnx、decoder_step.onnx 到 {ONNX_DIR}")

    crops = crops_from(Path(args.crops_from), Path(args.images), args.limit)
    reference = MangaOcrOnnx("cpu")
    for device in ("cpu", "dml"):
        cached = CachedDecoder(device)
        encoder = MangaOcrOnnx(device)
        mismatches = 0
        hidden = [encoder.encode(preprocess(crop)) for crop in crops]
        cached.greedy(np.concatenate(hidden[:BATCH]), reference.settings)  # 暖機
        start = time.perf_counter()
        got = []
        for first in range(0, len(crops), BATCH):
            got += cached.greedy(np.concatenate(hidden[first:first + BATCH]), reference.settings)
        elapsed = time.perf_counter() - start
        for crop, tokens in zip(crops, got):
            expected = decode_greedy(reference.encode(preprocess(crop)), reference.run_decoder,
                                     reference.settings)
            mismatches += tokens != expected
        print(f"2. {device}：{len(crops)} 張真實裁切圖（每批最多 {BATCH} 張），和沒有 cache 的版本"
              f"不同的 {mismatches} 張；解碼共 {elapsed * 1000:.0f} ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
