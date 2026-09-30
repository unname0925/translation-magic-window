"""量「字級」分得準不準（M2-17）：拿正確答案裡每個區塊標的小、中、大當基準。

每個正確區塊找出落在它裡面的 OCR 行（面積至少一半），用這些行估這個區塊的字有多大，
再和整張畫面所有行的中位數比，分成小、中、大。試兩種估法、幾組門檻，印出準確率和各級的召回率：

  cross：垂直於書寫方向的大小（橫排是行高、直排是欄寬）
  glyph：一個字的大小（橫排是行高、直排是欄長 ÷ 字數，和 core/text_layout 的 fontSize 相同）
  結尾加 w：整頁的中位數依每行的字數加權（網頁上大量的短選單文字不會把中位數拉小）

    tools/eval/.venv/Scripts/python tools/eval/evaluate_text_size.py \\
        --ocr build/ocr_eval/m2-17 --ground-truth testdata/private

注意：正確答案的字級是 draft_ground_truth.py 用「和整頁中位數比，< 0.7 倍小、> 1.5 倍大」
的規則起草、再人工校對的，所以結果會偏向那條規則。
報告含有截圖裡的文字，只放在本機。
"""

from __future__ import annotations

import argparse
import collections
import json
import statistics
import sys
from pathlib import Path

from evaluate_layout import overlap, read_ground_truth

SIZES = {"小": "small", "中": "normal", "大": "large"}


def read_sizes(path: Path) -> dict[str, list[str]]:
    """檔名 → 每個區塊的字級（和 read_ground_truth 的區塊順序相同）。"""
    sizes: dict[str, list[str]] = {}
    current = None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("== "):
            current = line[3:].strip()
            sizes[current] = []
        elif current and line.startswith("[") and "]" in line:
            parts = line[1:line.index("]")].split()
            if len(parts) == 4 and parts[2] in SIZES:
                sizes[current].append(SIZES[parts[2]])
    return sizes


def metric(line: dict, name: str) -> float:
    left, top, right, bottom = line["rect"]
    width, height = max(1, right - left), max(1, bottom - top)
    if not line["vertical"]:
        return height
    if name == "cross":
        return width
    return height / max(1, len(line["text"]))


def weighted_median(values: list[float], weights: list[float]) -> float:
    """加權中位數：依值排序後，累積權重先過一半的那個值。"""
    pairs = sorted(zip(values, weights))
    half, seen = sum(weights) / 2, 0.0
    for value, weight in pairs:
        seen += weight
        if seen >= half:
            return value
    return pairs[-1][0]


def classify(value: float, median: float, small: float, large: float) -> str:
    if value < median * small:
        return "small"
    if value > median * large:
        return "large"
    return "normal"


def product_size(inside: list[dict], blocks: list[dict]) -> str | None:
    """主程式實際給的字級：這些行被分到的段落（ocr_cli 的 blocks[].size），多數決。"""
    votes = collections.Counter()
    for line in inside:
        left, top, right, bottom = line["rect"]
        for block in blocks:
            bl, bt, br, bb = block["rect"]
            if left >= bl - 1 and top >= bt - 1 and right <= br + 1 and bottom <= bb + 1:
                if "size" in block:
                    votes[block["size"]] += 1
                break
    return votes.most_common(1)[0][0] if votes else None


def collect(ocr_dir: Path, gt_dir: Path) -> list[dict]:
    """每個有 OCR 行落在裡面的正確區塊一筆：類別、正確字級、各估法的值和整頁中位數。"""
    rows = []
    for result in sorted(ocr_dir.glob("*.json")):
        category = result.stem
        gt_path = gt_dir / category / "ground_truth.txt"
        if not gt_path.exists():
            continue
        pages = read_ground_truth(gt_path)
        sizes = read_sizes(gt_path)
        for image in json.loads(result.read_text(encoding="utf-8"))["images"]:
            blocks = pages.get(image["image"], [])
            lines = [line for line in image["text_lines"] if line["text"].strip()]
            if not blocks or not lines:
                continue
            # 每行一票：網頁上多數的行是選單和按鈕，中位數會被它們拉小。
            # 依字數加權：字多的本文佔比較重，按鈕上的兩三個字影響小。
            weights = [max(1, len(l["text"].strip())) for l in lines]
            medians = {}
            for name in ("cross", "glyph"):
                values = [metric(l, name) for l in lines]
                medians[name] = statistics.median(values)
                medians[f"{name}w"] = weighted_median(values, weights)
            for block, size in zip(blocks, sizes[image["image"]]):
                inside = []
                for line in lines:
                    left, top, right, bottom = line["rect"]
                    area = max(1, (right - left) * (bottom - top))
                    shared = (overlap(left, right, block["l"], block["r"]) *
                              overlap(top, bottom, block["t"], block["b"]))
                    if shared >= area * 0.5:
                        inside.append(line)
                if not inside:
                    continue
                rows.append({"category": category, "truth": size, "kind": block["kind"],
                             "product": product_size(inside, image.get("blocks", [])),
                             **{name: statistics.median(metric(l, base) for l in inside)
                                for name, base in (("cross", "cross"), ("glyph", "glyph"),
                                                   ("crossw", "cross"), ("glyphw", "glyph"))},
                             **{f"{name}_median": value for name, value in medians.items()}})
    return rows


def score(rows: list[dict], name: str, small: float, large: float) -> dict:
    confusion = collections.Counter()
    for row in rows:
        confusion[row["truth"], classify(row[name], row[f"{name}_median"], small, large)] += 1
    total = sum(confusion.values())
    correct = sum(v for (truth, guess), v in confusion.items() if truth == guess)
    recall = {}
    for level in ("small", "normal", "large"):
        support = sum(v for (truth, _), v in confusion.items() if truth == level)
        recall[level] = confusion[level, level] / support if support else 0.0
    return {"accuracy": correct / total if total else 0.0, "recall": recall,
            "confusion": confusion, "total": total}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ocr", required=True, help="每個分類一個 ocr_cli 結果的資料夾")
    parser.add_argument("--ground-truth", required=True, help="testdata/private")
    args = parser.parse_args()
    rows = collect(Path(args.ocr), Path(args.ground_truth))
    if not rows:
        print("沒有可以比對的區塊", file=sys.stderr)
        return 2
    print(f"{len(rows)} 個區塊：", dict(collections.Counter(r["truth"] for r in rows)))
    judged = [r for r in rows if r["product"]]
    if judged:
        # 主程式實際的結果：分級的單位是我們自己切出來的段落，不是正確答案的區塊
        confusion = collections.Counter((r["truth"], r["product"]) for r in judged)
        correct = sum(v for (a, b), v in confusion.items() if a == b)
        recall = []
        for level in ("small", "normal", "large"):
            support = sum(v for (a, _), v in confusion.items() if a == level)
            recall.append(f"{confusion[level, level] / support:.0%}" if support else "-")
        print(f"主程式實際（ocr_cli 的 size）：準確率 {correct / len(judged):.1%}，"
              f"小／中／大 的召回率 {'／'.join(recall)}")
        by_category = collections.defaultdict(list)
        for r in judged:
            by_category[r["category"]].append(r["truth"] == r["product"])
        print("　各分類：" + "、".join(f"{c} {sum(v) / len(v):.0%}"
                                     for c, v in sorted(by_category.items())))
    print(f"{'估法':6} {'小':>4} {'大':>4}  {'準確率':>6}  小／中／大 的召回率")
    for name in ("cross", "glyph", "crossw", "glyphw"):
        for small in (0.6, 0.7, 0.8):
            for large in (1.3, 1.5, 1.8):
                s = score(rows, name, small, large)
                r = s["recall"]
                print(f"{name:6} {small:4} {large:4}  {s['accuracy']:6.1%}  "
                      f"{r['small']:.0%}／{r['normal']:.0%}／{r['large']:.0%}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
