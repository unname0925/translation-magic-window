"""M0-11：日文漫畫的辨識評測。用正確答案的框裁切每個區塊（假設偵測完全正確），比較辨識模型。

    tools/eval/.venv-manga/Scripts/python tools/eval/evaluate_manga.py

比較：
- manga-ocr（ONNX，DirectML）：逐字解碼、beam search（官方的解碼方式）
- PP-OCR（C++ 的 tmw_ocr_cli）：在同一張裁切圖上偵測＋辨識，ルビ行用產品的規則去掉
- 兩者搭配（產品預定的規則，見 use_manga_ocr）：直排用 manga-ocr 逐字解碼，其餘用 PP-OCR v6-medium

裁切圖存在 build/ocr_eval/m0-11/crops/ja-manga（有版權，不進版本控制），也可以拿來跑
check_manga_ocr.py --crops，確認 ONNX 版在真實截圖上也和官方版完全相同。
報告寫在 build/ocr_eval/m0-11/manga_report.md。
"""

from __future__ import annotations

import argparse
import json
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

import ground_truth as gt
from evaluate_ocr import EVAL, PRIVATE, Summary, block_hypothesis, levenshtein, table, percent
from run_ocr import COMBOS, MODELS, OCR_CLI

CROPS = EVAL / "crops"
MARGIN = 4
PPOCR_COMBOS = ("v6-medium", "v5-server")
# 直排區塊的高超過寬的這個倍數（例如一整行的註解）時，manga-ocr 縮成 224×224 後讀不出來，
# 逐字解碼還會一直重複同樣的字，改用 PP-OCR
LONG_RATIO = 10


@dataclass
class Crop:
    name: str
    image: str
    index: int
    kind: str
    direction: str
    size: str
    width: int
    height: int
    reference: str  # 正規化後


def use_manga_ocr(crop: Crop) -> bool:
    """直排、而且不是極細長的單行，才用 manga-ocr。"""
    return crop.direction == "vertical" and crop.height <= LONG_RATIO * crop.width


def make_crops(category: str) -> list[Crop]:
    folder = CROPS / category
    folder.mkdir(parents=True, exist_ok=True)
    crops = []
    for page in gt.load(PRIVATE / category / "ground_truth.txt"):
        image = Image.open(PRIVATE / category / page.image).convert("RGB")
        for i, block in enumerate(page.blocks, start=1):
            if block.excluded or (block.language and block.language != "ja"):
                continue
            x0, y0, x1, y1 = block.box
            name = f"{Path(page.image).stem}_{i:02d}.png"
            image.crop((max(0, x0 - MARGIN), max(0, y0 - MARGIN), min(image.width, x1 + MARGIN),
                        min(image.height, y1 + MARGIN))).save(folder / name)
            crops.append(Crop(name, page.image, i, block.kind, block.direction, block.size,
                              x1 - x0, y1 - y0, gt.normalize(block.text("ja"))))
    return crops


def run_manga_ocr(crops: list[Crop], folder: Path) -> dict[str, dict]:
    import manga_onnx

    engine = manga_onnx.MangaOcrOnnx("dml")
    settings = engine.settings
    results = {}
    for crop in crops:
        image = Image.open(folder / crop.name)
        hidden = engine.encode(manga_onnx.preprocess(image))
        row = {}
        for method, decode in (("greedy", manga_onnx.decode_greedy),
                               ("beam", manga_onnx.decode_beam)):
            start = time.perf_counter()
            tokens = decode(hidden, engine.run_decoder, settings)
            row[method] = engine.detokenizer.decode(tokens)
            row[f"{method}_ms"] = (time.perf_counter() - start) * 1000
        results[crop.name] = row
    return results


def run_ppocr(combo: str, crops: list[Crop], folder: Path) -> dict[str, list[dict]]:
    output = folder.parent / f"{folder.name}.{combo}.json"
    detection, recognition = COMBOS[combo]
    subprocess.run([str(OCR_CLI), "--det", str(MODELS / detection), "--rec", str(MODELS / recognition),
                    "--device", "dml", "--output", str(output),
                    *(str(folder / crop.name) for crop in crops)],
                   check=True, capture_output=True)
    raw = json.loads(output.read_text(encoding="utf-8"))
    return {image["image"]: image["lines"] for image in raw["images"]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--category", default="ja-manga")
    args = parser.parse_args()

    crops = make_crops(args.category)
    folder = CROPS / args.category
    print(f"{len(crops)} 個區塊")
    manga = run_manga_ocr(crops, folder)
    ppocr = {combo: run_ppocr(combo, crops, folder) for combo in PPOCR_COMBOS}

    methods = ["manga-ocr 逐字解碼", "manga-ocr beam search", *(f"PP-OCR {c}" for c in PPOCR_COMBOS),
               "直排 manga-ocr＋其他 v6-medium"]
    rows = []  # 每個區塊、每種方法的 (距離, 結果)
    for crop in crops:
        block = gt.Block(crop.kind, crop.direction, crop.size, (0, 0, 1, 1), ["x"])
        hypotheses = [gt.normalize(manga[crop.name]["greedy"]), gt.normalize(manga[crop.name]["beam"])]
        hypotheses += [gt.normalize(block_hypothesis(block, ppocr[c][crop.name], "ja"))
                       for c in PPOCR_COMBOS]
        hypotheses.append(hypotheses[0] if use_manga_ocr(crop) else hypotheses[2])  # 2：v6-medium
        rows.append((crop, hypotheses))

    def summary(selected, method: int) -> Summary | None:
        if not selected:
            return None
        characters = sum(len(c.reference) for c, _ in selected)
        return Summary(len(selected), characters,
                       sum(levenshtein(c.reference, h[method]) for c, h in selected) / max(1, characters),
                       sum(c.reference == h[method] for c, h in selected) / len(selected), 0.0)

    groups = {"全部": rows, "擬聲詞以外": [(c, h) for c, h in rows if c.kind != "擬聲詞"]}
    for key in ("kind", "direction", "size"):
        for crop, hypotheses in rows:
            groups.setdefault(f"{key}：{getattr(crop, key)}", []).append((crop, hypotheses))
    out = [f"# M0-11 {args.category} 辨識評測（用正確答案的框裁切，自動產生）", "",
           "由 `tools/eval/evaluate_manga.py` 產生。CER 是字元錯誤率；括號是完全正確的區塊比例。", ""]
    out += table(["分組", "區塊", *methods],
                 [[name, str(len(selected)), *(
                     f"{percent(s.cer)}（{percent(s.exact)}）" for s in
                     (summary(selected, m) for m in range(len(methods))))]
                  for name, selected in groups.items()])
    greedy_ms = sorted(manga[c.name]["greedy_ms"] for c in crops)
    beam_ms = sorted(manga[c.name]["beam_ms"] for c in crops)
    out += ["", f"manga-ocr 解碼耗時（DirectML，不含編碼器，中位數）：逐字解碼 "
                f"{greedy_ms[len(greedy_ms) // 2]:.0f} ms、beam search {beam_ms[len(beam_ms) // 2]:.0f} ms", ""]
    out += ["## 逐字解碼和 beam search 結果不同的區塊", ""]
    out += table(["區塊", "種類", "正確答案", "逐字解碼", "beam search"],
                 [[f"{c.image[-10:]} #{c.index}", c.kind, c.reference, h[0], h[1]]
                  for c, h in rows if h[0] != h[1]])
    out += ["", "## 各方法錯最多的區塊", ""]
    for m, method in enumerate(methods):
        worst = sorted(rows, key=lambda x: levenshtein(x[0].reference, x[1][m]), reverse=True)[:10]
        out += [f"### {method}", ""]
        out += table(["區塊", "種類", "錯", "正確答案", "辨識結果"],
                     [[f"{c.image[-10:]} #{c.index}", c.kind, str(levenshtein(c.reference, h[m])),
                       c.reference[:30], h[m][:30] or "（沒有）"] for c, h in worst
                      if levenshtein(c.reference, h[m])]) + [""]
    output = EVAL / "manga_report.md"
    output.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    (EVAL / "manga_results.json").write_text(json.dumps(
        [{"crop": c.name, "reference": c.reference, **dict(zip(methods, h))} for c, h in rows],
        ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"報告：{output}")


if __name__ == "__main__":
    main()
