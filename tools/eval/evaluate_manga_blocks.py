"""M2-03 動工前的實驗：在「產品實際會切出來的區塊」上，manga-ocr 比 PP-OCR 準多少？

M0-11 的 evaluate_manga.py 用正確答案的框去切圖（假設切割完全正確）。產品切的是我們自己分段
（漫畫模式：依對話框）出來的區塊：框比較緊、不一定包到ルビ、偶爾合併過頭。這裡在那些區塊上比。

只比「剛好對上一個正確單位」的區塊（evaluate_layout.py 的正確復原），這樣比文字才公平；
同句（`同句:`）的幾個區塊算一個單位，參考答案是它們的文字依檔案順序接起來。
直排、而且高不超過寬 10 倍的區塊才用 manga-ocr（M0-11 的規則），其餘沿用 PP-OCR。

    tools/eval/.venv-manga/Scripts/python tools/eval/evaluate_manga_blocks.py \\
        --ocr build/ocr_eval/m2-02/with_ctd.json --images testdata/private/ja-manga \\
        --ground-truth testdata/private/ja-manga/ground_truth.txt

報告含有截圖裡的文字，只印在本機。
"""

from __future__ import annotations

import argparse
import collections
import json
import sys
import time
from pathlib import Path

from PIL import Image

import ground_truth as gt
from evaluate_layout import overlap
from evaluate_ocr import levenshtein
from manga_onnx import MangaOcrOnnx

MARGIN = 4
LONG_RATIO = 10


def contained(line, block) -> bool:
    left, top, right, bottom = line
    bl, bt, br, bb = block
    return left >= bl - 1 and top >= bt - 1 and right <= br + 1 and bottom <= bb + 1


def unit_of_line(rect, blocks: list[gt.Block]) -> int | None:
    """這一行落在哪個正確區塊裡（至少一半面積），回傳區塊的索引。"""
    left, top, right, bottom = rect
    area = max(1, (right - left) * (bottom - top))
    best, best_shared = None, 0
    for index, block in enumerate(blocks):
        x0, y0, x1, y1 = block.box
        shared = overlap(left, right, x0, x1) * overlap(top, bottom, y0, y1)
        if shared > best_shared:
            best, best_shared = index, shared
    return best if best_shared >= area * 0.5 else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ocr", required=True)
    parser.add_argument("--images", required=True)
    parser.add_argument("--ground-truth", required=True)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--show", type=int, default=8, help="列出幾個差最多的例子")
    args = parser.parse_args()

    pages = {page.image: page for page in gt.load(Path(args.ground_truth))}
    images = json.loads(Path(args.ocr).read_text(encoding="utf-8"))["images"]
    model = MangaOcrOnnx(args.device)

    totals = collections.Counter()
    seconds = []
    examples = []
    for image in images:
        page = pages.get(image["image"])
        if page is None:
            continue
        blocks = page.blocks
        unit = [("句", b.sentence) if b.sentence else ("塊", i) for i, b in enumerate(blocks)]
        with Image.open(Path(args.images) / image["image"]) as opened:
            picture = opened.convert("RGB")

        # 每個我們的段落裡的行，各落在哪個正確單位
        members = collections.defaultdict(set)
        for line in image["text_lines"]:
            for index, block in enumerate(image["blocks"]):
                if contained(line["rect"], block["rect"]):
                    gt_index = unit_of_line(line["rect"], blocks)
                    if gt_index is not None:
                        members[index].add(unit[gt_index])
                    break
        owners = collections.defaultdict(set)
        for index, units in members.items():
            for u in units:
                owners[u].add(index)

        for index, block in enumerate(image["blocks"]):
            units = members.get(index, set())
            if len(units) != 1:
                continue
            (u,) = units
            if owners[u] != {index}:
                continue  # 這個單位被切成好幾段，不是 1:1
            parts = [b for b, key in zip(blocks, unit) if key == u]
            if any(b.excluded for b in parts) or any(b.kind not in ("對白", "旁白") for b in parts):
                continue
            reference = gt.normalize("".join("".join(b.lines) for b in parts))
            if not reference:
                continue
            left, top, right, bottom = block["rect"]
            width, height = right - left, bottom - top
            if not block["vertical"] or height > LONG_RATIO * width:
                totals["橫排或太細長（沿用 PP-OCR）"] += 1
                continue

            crop = picture.crop((max(0, left - MARGIN), max(0, top - MARGIN),
                                 min(picture.width, right + MARGIN),
                                 min(picture.height, bottom + MARGIN)))
            start = time.perf_counter()
            manga_text, _ = model(crop, method="greedy")
            seconds.append(time.perf_counter() - start)

            ppocr = gt.normalize(block["text"])
            manga = gt.normalize(manga_text)
            ppocr_errors = levenshtein(ppocr, reference)
            manga_errors = levenshtein(manga, reference)
            totals["區塊"] += 1
            totals["字數"] += len(reference)
            totals["PP-OCR 錯字"] += ppocr_errors
            totals["manga-ocr 錯字"] += manga_errors
            totals["PP-OCR 全對"] += ppocr_errors == 0
            totals["manga-ocr 全對"] += manga_errors == 0
            examples.append((abs(ppocr_errors - manga_errors), image["image"][-10:], reference,
                             ppocr, manga))

    chars = max(1, totals["字數"])
    blocks_count = max(1, totals["區塊"])
    print(f"直排、1:1 對上正確單位的區塊：{totals['區塊']} 個，{totals['字數']} 字"
          f"（另有 {totals['橫排或太細長（沿用 PP-OCR）']} 個橫排或太細長的沿用 PP-OCR）")
    print(f"PP-OCR    字元錯誤率 {totals['PP-OCR 錯字'] / chars:6.1%}　全對 "
          f"{totals['PP-OCR 全對']}/{totals['區塊']}（{totals['PP-OCR 全對'] / blocks_count:.0%}）")
    print(f"manga-ocr 字元錯誤率 {totals['manga-ocr 錯字'] / chars:6.1%}　全對 "
          f"{totals['manga-ocr 全對']}/{totals['區塊']}（{totals['manga-ocr 全對'] / blocks_count:.0%}）")
    if seconds:
        seconds.sort()
        print(f"manga-ocr 每個區塊（{args.device}，沒有 KV cache 的逐字解碼）：中位數 "
              f"{seconds[len(seconds) // 2] * 1000:.0f} ms，最長 {seconds[-1] * 1000:.0f} ms")
    print("\n差最多的例子（參考答案 ／ PP-OCR ／ manga-ocr）：")
    for _, name, reference, ppocr, manga in sorted(examples, reverse=True)[:args.show]:
        print(f"  {name}  {reference}\n            P: {ppocr}\n            M: {manga}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
