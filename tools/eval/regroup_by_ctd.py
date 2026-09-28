"""M2-02 動工前的實驗：用 comic-text-detector 的對話框區塊來分段，會比現在的分段好嗎？

拿 ocr_cli 的結果（每一行的位置、現在的分段），加上 comic-text-detector 在同一頁找到的
文字區塊，組出幾種不同的分段，全部用 evaluate_layout.py 的同一把尺（正確答案）打分：

  A 現在的分段          ocr_cli 輸出的 blocks，原封不動
  B 只看對話框          每一行歸到蓋住它一半以上的區塊；沒被蓋住的行維持原本的段落
  C 聯集                同一段「或」同一個區塊就併在一起（看合併得回來多少，也看會不會併過頭）
  D 交集                原本的段落，再依區塊切開（專治合併過頭）

評分的規則和 evaluate_layout.py 相同，只差在「每一行屬於我們的哪一段」直接用分組的結果，
不用外框的包含關係去猜——不同分法的外框可能互相重疊，用猜的會讓比較失真。

    tools/eval/.venv-manga/Scripts/python tools/eval/regroup_by_ctd.py \\
        --ocr build/ocr_eval/m2-02/baseline.json \\
        --images testdata/private/ja-manga \\
        --ground-truth testdata/private/ja-manga/ground_truth.txt

報告含有截圖裡的文字位置，只放在本機。
"""

from __future__ import annotations

import argparse
import collections
import json
import sys
from pathlib import Path

from PIL import Image

from comic_text_detector import ComicTextDetector
from evaluate_layout import overlap, read_ground_truth


def area(rect) -> int:
    return max(1, (rect[2] - rect[0]) * (rect[3] - rect[1]))


def shared(a, b) -> int:
    return overlap(a[0], a[2], b[0], b[2]) * overlap(a[1], a[3], b[1], b[3])


def original_membership(image: dict) -> list[int | None]:
    """每一行在 ocr_cli 的哪一段（和 evaluate_layout.py 一樣用外框包含關係）。"""
    out = []
    for line in image["text_lines"]:
        left, top, right, bottom = line["rect"]
        found = None
        for index, block in enumerate(image["blocks"]):
            bl, bt, br, bb = block["rect"]
            if left >= bl - 1 and top >= bt - 1 and right <= br + 1 and bottom <= bb + 1:
                found = index
                break
        out.append(found)
    return out


def ctd_membership(image: dict, boxes: list[tuple[int, int, int, int]]) -> list[int | None]:
    """每一行被哪個對話框區塊蓋住一半以上（蓋最多的那個）。"""
    out = []
    for line in image["text_lines"]:
        rect = line["rect"]
        best, best_shared = None, 0
        for index, box in enumerate(boxes):
            s = shared(rect, box)
            if s > best_shared:
                best, best_shared = index, s
        out.append(best if best_shared >= area(rect) * 0.5 else None)
    return out


def union_groups(orig: list, ctd: list) -> list[int]:
    """同一段或同一個區塊就連起來（union-find）。"""
    parent = list(range(len(orig)))

    def find(i: int) -> int:
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for key_list in (orig, ctd):
        first: dict = {}
        for i, key in enumerate(key_list):
            if key is None:
                continue
            if key in first:
                parent[find(i)] = find(first[key])
            else:
                first[key] = i
    return [find(i) for i in range(len(orig))]


def score(gt_blocks: list[dict], lines: list[dict], groups: list) -> collections.Counter:
    """evaluate_layout.evaluate 的評分，但「我們的段落」直接用 groups。"""
    totals = collections.Counter()
    gt_of = []
    for line in lines:
        rect = line["rect"]
        best, best_shared = None, 0
        for index, block in enumerate(gt_blocks):
            s = shared(rect, (block["l"], block["t"], block["r"], block["b"]))
            if s > best_shared:
                best, best_shared = index, s
        gt_of.append(best if best_shared >= area(rect) * 0.5 else None)

    totals["正確區塊"] += len(gt_blocks)
    totals["我們的段落"] += len({g for g in groups if g is not None})
    by_gt = collections.defaultdict(set)
    by_ours = collections.defaultdict(set)
    for gt, ours in zip(gt_of, groups):
        if gt is not None and ours is not None:
            by_gt[gt].add(ours)
            by_ours[ours].add(gt)
    for gt, ours in by_gt.items():
        if len(ours) == 1 and len(by_ours[next(iter(ours))]) == 1:
            totals["正確復原"] += 1
        elif len(ours) > 1:
            totals["被切開"] += 1
    totals["合併過頭"] += sum(1 for gts in by_ours.values() if len(gts) > 1)
    return totals


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ocr", required=True)
    parser.add_argument("--images", required=True)
    parser.add_argument("--ground-truth", required=True)
    parser.add_argument("--device", default="dml")
    args = parser.parse_args()

    pages = read_ground_truth(Path(args.ground_truth))
    images = json.loads(Path(args.ocr).read_text(encoding="utf-8"))["images"]
    detector = ComicTextDetector(args.device)

    variants = {"A 現在的分段": collections.Counter(), "B 只看對話框": collections.Counter(),
                "C 聯集": collections.Counter(), "D 交集": collections.Counter()}
    uncovered = 0
    total_lines = 0
    for image in images:
        name = image["image"]
        if name not in pages:
            continue
        with Image.open(Path(args.images) / name) as page:
            detections, _ = detector(page)
        boxes = [d.box for d in detections]
        orig = original_membership(image)
        ctd = ctd_membership(image, boxes)
        total_lines += len(orig)
        uncovered += sum(1 for c in ctd if c is None)

        groupings = {
            "A 現在的分段": [("o", o) if o is not None else None for o in orig],
            "B 只看對話框": [("c", c) if c is not None else (("o", o) if o is not None else None)
                             for o, c in zip(orig, ctd)],
            "C 聯集": union_groups(orig, ctd),
            "D 交集": [("o", o, c) if o is not None else None for o, c in zip(orig, ctd)],
        }
        for label, groups in groupings.items():
            variants[label] += score(pages[name], image["text_lines"], groups)

    print(f"共 {total_lines} 行，其中 {uncovered} 行沒有被任何對話框區塊蓋住")
    print(f"{'分法':<14}{'段落':>6}{'正確復原':>10}{'被切開':>8}{'合併過頭':>10}")
    for label, totals in variants.items():
        print(f"{label:<14}{totals['我們的段落']:>6}{totals['正確復原']:>10}"
              f"{totals['被切開']:>8}{totals['合併過頭']:>10}")
    print(f"（正確區塊共 {next(iter(variants.values()))['正確區塊']} 個）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
