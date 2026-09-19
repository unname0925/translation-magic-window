"""M0-15 的驗收：manga-ocr 匯出成 ONNX、自己寫解碼之後，結果要和官方實作完全相同。

每張對話框圖片：
1. 官方 manga-ocr（PyTorch，transformers 4.57.6）的結果當參考答案：文字，以及 generate 產生的 token。
2. 檢查前處理：manga_onnx.preprocess 和官方的 ViTImageProcessor 結果必須完全相同。
3. ONNX Runtime（CPU、DirectML）＋ manga_onnx.decode_beam：token 和文字都必須和官方完全相同。
4. 逐字解碼（greedy）：只記錄結果和速度，用來評估是否值得保留 beam search。

必須用 tools/eval/.venv-manga 的 Python 執行：
    tools/eval/.venv-manga/Scripts/python tools/eval/check_manga_ocr.py [--crops <資料夾>]

預設用合成的對話框（make_manga_crops.py）；--crops 改用資料夾裡的圖片，例如 evaluate_manga.py
從真實截圖裁切出來的區塊（build/ocr_eval/m0-11/crops/ja-manga）。
結果寫在 build/manga_ocr/report.md 和 results.json（--crops 時寫在那個資料夾旁邊）。
"""

from __future__ import annotations

import argparse
import json
import os
import statistics
import sys
import time
from pathlib import Path

os.environ.setdefault("HF_HUB_OFFLINE", "1")  # 只用本機的模型檔

import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402

import make_manga_crops  # noqa: E402
import manga_onnx  # noqa: E402

OUTPUT_DIR = manga_onnx.REPO_ROOT / "build" / "manga_ocr"


def trim(tokens: list[int], settings: manga_onnx.GenerationSettings) -> list[int]:
    """去掉結尾補的 pad（官方 generate 的輸出會補到同一個長度）。"""
    end = len(tokens)
    while end > 0 and tokens[end - 1] == settings.pad_token_id:
        end -= 1
    return tokens[:end]


def timed(function, *args, **kwargs):
    start = time.perf_counter()
    result = function(*args, **kwargs)
    return result, (time.perf_counter() - start) * 1000


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--crops", type=Path, help="改用這個資料夾裡的圖片")
    args = parser.parse_args()
    output_dir = OUTPUT_DIR
    if args.crops:
        crops_dir = args.crops
        output_dir = crops_dir.parent / f"{crops_dir.name}.check_manga_ocr"
    else:
        crops_dir = OUTPUT_DIR / "crops"
        if not crops_dir.exists():
            crops_dir.mkdir(parents=True)
            for name, image in make_manga_crops.crops().items():
                image.save(crops_dir / f"{name}.png")
    if not (manga_onnx.ONNX_DIR / "decoder.onnx").exists():
        print("exporting ONNX ...")
        manga_onnx.export()

    import manga_ocr.ocr as official_module
    import torch
    from manga_ocr import MangaOcr

    images = sorted(crops_dir.glob("*.png"))
    if not args.crops:
        images.append(Path(official_module.__file__).parent / "assets" / "example.jpg")

    official = MangaOcr(str(manga_onnx.MODEL_DIR), force_cpu=True)
    settings = manga_onnx.GenerationSettings.load()
    engines = {"cpu": manga_onnx.MangaOcrOnnx("cpu"), "dml": manga_onnx.MangaOcrOnnx("dml")}

    rows = []
    problems = []
    for path in images:
        image = Image.open(path)
        row = {"image": path.name}

        # 1. 官方
        pixel_values = official._preprocess(image.convert("L").convert("RGB"))
        with torch.no_grad():
            ids, ms = timed(official.model.generate, pixel_values[None], max_length=300)
        row["official_tokens"] = trim(ids[0].tolist(), settings)
        row["official_text"] = official(image)
        row["official_ms"] = ms

        # 2. 前處理
        difference = float(np.abs(manga_onnx.preprocess(image) - pixel_values.numpy()[None]).max())
        row["preprocess_max_diff"] = difference
        if difference != 0.0:
            problems.append(f"{path.name}: preprocessing differs by {difference}")

        # 3. ONNX + beam search
        for device, engine in engines.items():
            engine(image)  # 暖機（DirectML 第一次遇到新的輸入大小時要編譯）
            hidden, encode_ms = timed(engine.encode, manga_onnx.preprocess(image))
            tokens, decode_ms = timed(manga_onnx.decode_beam, hidden, engine.run_decoder, settings)
            text = engine.detokenizer.decode(tokens)
            row[f"{device}_tokens"] = tokens
            row[f"{device}_text"] = text
            row[f"{device}_encode_ms"] = encode_ms
            row[f"{device}_decode_ms"] = decode_ms
            if tokens != row["official_tokens"]:
                problems.append(f"{path.name} ({device}): tokens {tokens} vs official "
                                f"{row['official_tokens']}")
            if text != row["official_text"]:
                problems.append(f"{path.name} ({device}): text {text!r} vs official "
                                f"{row['official_text']!r}")

        # 4. 逐字解碼（只記錄）
        for device, engine in engines.items():
            hidden = engine.encode(manga_onnx.preprocess(image))
            tokens, ms = timed(manga_onnx.decode_greedy, hidden, engine.run_decoder, settings)
            row[f"{device}_greedy_text"] = engine.detokenizer.decode(tokens)
            row[f"{device}_greedy_decode_ms"] = ms

        same = "✅" if row["cpu_text"] == row["official_text"] and \
            row["dml_text"] == row["official_text"] else "❌"
        print(f"{same} {path.name}: {row['official_text']}")
        rows.append(row)

    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "results.json").write_text(json.dumps(rows, ensure_ascii=False, indent=2),
                                             encoding="utf-8")

    def median(key):
        return statistics.median(row[key] for row in rows)

    greedy_same = sum(row["cpu_greedy_text"] == row["official_text"] for row in rows)
    report = [
        "| 圖片 | 官方結果 | ONNX CPU | ONNX DirectML | 逐字解碼 |",
        "|---|---|---|---|---|",
    ]
    for row in rows:
        report.append(
            f"| {row['image']} | {row['official_text']} "
            f"| {'✅' if row['cpu_text'] == row['official_text'] else '❌'} "
            f"| {'✅' if row['dml_text'] == row['official_text'] else '❌'} "
            f"| {'相同' if row['cpu_greedy_text'] == row['official_text'] else row['cpu_greedy_text']} |")
    report += [
        "",
        "| 耗時（中位數） | CPU | DirectML |",
        "|---|---|---|",
        f"| 編碼器 | {median('cpu_encode_ms'):.1f} ms | {median('dml_encode_ms'):.1f} ms |",
        f"| beam search 解碼 | {median('cpu_decode_ms'):.1f} ms | {median('dml_decode_ms'):.1f} ms |",
        f"| 逐字解碼 | {median('cpu_greedy_decode_ms'):.1f} ms | "
        f"{median('dml_greedy_decode_ms'):.1f} ms |",
        f"| 官方（PyTorch CPU，含編碼） | {median('official_ms'):.1f} ms | — |",
        "",
        f"逐字解碼和官方（beam search）結果相同：{greedy_same}／{len(rows)} 張",
    ]
    (output_dir / "report.md").write_text("\n".join(report) + "\n", encoding="utf-8")
    print("\n" + "\n".join(report))
    if problems:
        print(f"\nFAIL: {len(problems)} problem(s)")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print("\nPASS: ONNX + own beam search matches official manga-ocr exactly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
