"""量「分段」做得好不好：拿正確答案的區塊座標當基準。

段落數本身不是好指標——放寬門檻可以讓段落數變少，但正確復原的區塊也會跟著變少、
合併過頭的變多。所以這裡量的是：

  正確復原：某個正確區塊裡的每一行，最後都落在「同一個」我們的段落裡，
            而且那個段落沒有混進別的區塊的行。
  合併過頭：我們的一個段落裡混到了兩個以上的正確區塊。

    tools/eval/.venv/Scripts/python tools/eval/evaluate_layout.py \
        --ocr <ocr_cli 產生的 result.json> --ground-truth testdata/private/ja-manga/ground_truth.txt

ocr_cli 的輸出要含有 blocks 和 text_lines 這兩個欄位。
報告含有截圖裡的文字，只放在本機。
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import sys
from pathlib import Path


def read_ground_truth(path: Path) -> dict[str, list[dict]]:
    """檔名 → 區塊清單。格式見 tools/eval/ground_truth.py。"""
    pages: dict[str, list[dict]] = {}
    current = None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("== "):
            current = line[3:].strip()
            pages[current] = []
        found = re.match(r"^\[(\S+)\s+(\S+)\s+(\S+)\s+(\d+),(\d+),(\d+),(\d+)\]", line)
        if found and current:
            pages[current].append({
                "kind": found.group(1),
                "l": int(found.group(4)), "t": int(found.group(5)),
                "r": int(found.group(6)), "b": int(found.group(7)),
            })
    return pages


def overlap(a1: int, a2: int, b1: int, b2: int) -> int:
    return max(0, min(a2, b2) - max(a1, b1))


def evaluate(pages: dict[str, list[dict]], images: dict[str, dict]) -> dict:
    totals = collections.Counter()
    split_kinds = collections.Counter()
    for name, blocks in pages.items():
        image = images.get(name)
        if image is None:
            continue
        totals["正確區塊"] += len(blocks)

        # 每一行落在哪個正確區塊裡（至少一半的面積）
        lines = []
        for line in image["text_lines"]:
            left, top, right, bottom = line["rect"]
            area = max(1, (right - left) * (bottom - top))
            best, best_overlap = None, 0
            for index, block in enumerate(blocks):
                shared = (overlap(left, right, block["l"], block["r"]) *
                          overlap(top, bottom, block["t"], block["b"]))
                if shared > best_overlap:
                    best, best_overlap = index, shared
            lines.append({"rect": line["rect"], "gt": best if best_overlap >= area * 0.5 else None})
        totals["我們的行"] += len(lines)
        totals["落在正確區塊內的行"] += sum(1 for x in lines if x["gt"] is not None)

        # 每一行屬於我們的哪一段：用座標包含關係對應
        for index, line in enumerate(lines):
            left, top, right, bottom = line["rect"]
            line["ours"] = None
            for block_index, block in enumerate(image["blocks"]):
                bl, bt, br, bb = block["rect"]
                if left >= bl - 1 and top >= bt - 1 and right <= br + 1 and bottom <= bb + 1:
                    line["ours"] = block_index
                    break
        totals["我們的段落"] += len(image["blocks"])

        by_gt = collections.defaultdict(list)
        for line in lines:
            if line["gt"] is not None and line["ours"] is not None:
                by_gt[line["gt"]].append(line)
        by_ours = collections.defaultdict(set)
        for line in lines:
            if line["ours"] is not None and line["gt"] is not None:
                by_ours[line["ours"]].add(line["gt"])

        for gt_index, members in by_gt.items():
            ours = {x["ours"] for x in members}
            if len(ours) == 1 and len(by_ours[next(iter(ours))]) == 1:
                totals["正確復原"] += 1
            elif len(ours) > 1:
                totals["被切開"] += 1
                split_kinds[blocks[gt_index]["kind"]] += 1
        totals["合併過頭"] += sum(1 for gts in by_ours.values() if len(gts) > 1)
    return {"totals": dict(totals), "被切開的種類": dict(split_kinds)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ocr", required=True)
    parser.add_argument("--ground-truth", required=True)
    args = parser.parse_args()

    pages = read_ground_truth(Path(args.ground_truth))
    images = {i["image"]: i for i in json.loads(Path(args.ocr).read_text(encoding="utf-8"))["images"]}
    if not pages:
        print("讀不到正確答案", file=sys.stderr)
        return 2
    result = evaluate(pages, images)
    totals = result["totals"]
    for key in ("正確區塊", "我們的段落", "我們的行", "落在正確區塊內的行", "正確復原",
                "被切開", "合併過頭"):
        print(f"{key:<18}{totals.get(key, 0)}")
    if result["被切開的種類"]:
        print("被切開的種類:", result["被切開的種類"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
