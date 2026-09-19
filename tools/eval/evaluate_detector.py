"""M0-11：漫畫文字偵測的評測。comic-text-detector 和 PP-OCR 的偵測模型，各找到多少正確答案的區塊。

    tools/eval/.venv-manga/Scripts/python tools/eval/evaluate_detector.py

一個區塊算「偵測到」：comic-text-detector 的某個文字區塊蓋住它一半以上的面積；PP-OCR 則是
落在它裡面的文字行（重疊超過行的一半，不論有沒有辨識出字）合起來蓋住它一半以上的面積；
comic-text-detector 的文字遮罩（seg 輸出，大於 MASK_THRESHOLD 的像素）蓋住它 MASK_COVERAGE 以上的面積
（字的筆畫只占區塊的一部分，所以門檻比較低；另外統計區塊外被標成文字的像素比例，看遮罩會不會亂標）。
「多出來」是沒有和任何正確答案區塊（包含不評測的）重疊一半以上的偵測結果。
報告寫在 build/ocr_eval/m0-11/detector_report.md，偵測結果的圖在 build/ocr_eval/m0-11/detector/。
"""

from __future__ import annotations

import argparse
import time

import numpy as np
from PIL import Image, ImageDraw

import ground_truth as gt
from comic_text_detector import ComicTextDetector
from evaluate_ocr import EVAL, PRIVATE, intersection, line_rect, load_raw, percent, table

MASK_THRESHOLD = 0.3
MASK_COVERAGE = 0.15
PPOCR_COMBOS = {"ja": ("v6-medium", "v5-server"), "en": ("v6-medium", "v5-server"),
                "ko": ("ko-v6-medium-det", "ko-v5-server-det")}


def area(box) -> float:
    return max(0, box[2] - box[0]) * max(0, box[3] - box[1])


def covered_by_lines(box, rects: list[tuple]) -> float:
    """文字行合起來蓋住 box 的比例（用點陣計算，避免重疊的行重複計算）。"""
    x0, y0, x1, y1 = (int(v) for v in box)
    if x1 <= x0 or y1 <= y0:
        return 0.0
    grid = np.zeros((y1 - y0, x1 - x0), dtype=bool)
    for r in rects:
        a, b = max(x0, int(r[0])), max(y0, int(r[1]))
        c, d = min(x1, int(np.ceil(r[2]))), min(y1, int(np.ceil(r[3])))
        if c > a and d > b:
            grid[b - y0:d - y0, a - x0:c - x0] = True
    return float(grid.mean())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--categories", nargs="+", default=["ja-manga", "en-manga", "ko-manga"])
    args = parser.parse_args()

    detector = ComicTextDetector("dml")
    rows = {}  # (分類, 分組) → {方法: [偵測到, 全部]}
    extra = {}  # (分類, 方法) → 多出來的數量
    timings = []
    background = {}  # 分類 → 每頁區塊外被遮罩標成文字的像素比例
    for category in args.categories:
        language = category.split("-")[0]
        pages = gt.load(PRIVATE / category / "ground_truth.txt")
        raws = {combo: load_raw("dml", combo, category) for combo in PPOCR_COMBOS[language]}
        methods = ["comic-text-detector", "文字遮罩", *raws]
        for page in pages:
            image = Image.open(PRIVATE / category / page.image).convert("RGB")
            detector(image)  # 暖機
            start = time.perf_counter()
            detections, mask = detector(image)
            timings.append((time.perf_counter() - start) * 1000)
            text_pixels = mask > MASK_THRESHOLD
            inside = np.zeros(text_pixels.shape, dtype=bool)
            for block in page.blocks:
                inside[block.box[1]:block.box[3], block.box[0]:block.box[2]] = True
            background.setdefault(category, []).append(float(text_pixels[~inside].mean()))
            found = {  # 方法 → 區塊有沒有偵測到
                "comic-text-detector": lambda b: max(
                    (intersection(d.box, b.box) for d in detections), default=0) >= 0.5 * area(b.box),
                "文字遮罩": lambda b: text_pixels[b.box[1]:b.box[3], b.box[0]:b.box[2]].mean() >= MASK_COVERAGE}
            for combo, raw in raws.items():
                lines = next(i for i in raw["images"] if i["image"] == page.image)["lines"]
                rects = [line_rect(line) for line in lines]
                found[combo] = lambda b, rects=rects: covered_by_lines(b.box, [
                    r for r in rects if intersection(r, b.box) > 0.5 * max(1e-6, area(r))]) >= 0.5
            for block in page.blocks:
                if block.excluded:
                    continue
                for group in ("全部", f"種類：{block.kind}", f"字級：{block.size}"):
                    counts = rows.setdefault((category, group), {m: [0, 0] for m in methods})
                    for method in methods:
                        counts[method][0] += bool(found[method](block))
                        counts[method][1] += 1
            boxes = [b.box for b in page.blocks]
            misses = [d for d in detections
                      if max((intersection(d.box, b) for b in boxes), default=0) < 0.5 * area(d.box)]
            extra[category, "comic-text-detector"] = extra.get((category, "comic-text-detector"), 0) + \
                len(misses)

            draw = ImageDraw.Draw(image)
            for block in page.blocks:
                draw.rectangle(block.box, outline=(128, 128, 128) if block.excluded else (0, 170, 0),
                               width=2)
            for d in detections:
                draw.rectangle(d.box, outline=(230, 0, 0) if d.label == 0 else (0, 90, 255), width=3)
            output = EVAL / "detector" / category / f"{page.image.rsplit('.', 1)[0]}.png"
            output.parent.mkdir(parents=True, exist_ok=True)
            image.save(output)

    out = ["# M0-11 漫畫文字偵測（自動產生）", "",
           "由 `tools/eval/evaluate_detector.py` 產生。表中是偵測到的正確答案區塊比例（偵測到／全部）。",
           f"comic-text-detector 耗時（DirectML，每頁中位數）：{sorted(timings)[len(timings) // 2]:.0f} ms。",
           "偵測結果的圖：綠框是正確答案（灰框不評測），紅框和藍框是 comic-text-detector 的兩個類別。",
           f"文字遮罩：值大於 {MASK_THRESHOLD} 的像素蓋住區塊 {MASK_COVERAGE:.0%} 以上的面積就算偵測到。", ""]
    for category in args.categories:
        language = category.split("-")[0]
        methods = ["comic-text-detector", "文字遮罩", *PPOCR_COMBOS[language]]
        groups = sorted((g for c, g in rows if c == category), key=lambda g: (g != "全部", g))
        out += [f"## {category}", ""]
        out += table(["分組", *methods], [[group, *(
            f"{percent(rows[category, group][m][0] / rows[category, group][m][1])}"
            f"（{rows[category, group][m][0]}/{rows[category, group][m][1]}）" for m in methods)]
            for group in groups])
        out += ["", f"comic-text-detector 多出來的區塊：{extra.get((category, 'comic-text-detector'), 0)} 個；"
                f"區塊外被文字遮罩標成文字的像素：{percent(float(np.mean(background[category])))}（每頁平均）", ""]
    report = EVAL / "detector_report.md"
    report.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    print(f"報告：{report}")


if __name__ == "__main__":
    main()
