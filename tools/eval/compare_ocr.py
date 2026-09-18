"""比對兩份 OCR 結果（例如 Python 參考版和 C++ 版），檢查 M0-14 的驗收條件：
同一張圖片，文字必須完全一致，文字框每個角的座標誤差不超過 --box-tolerance 像素。

用法：
    python tools/eval/compare_ocr.py <參考.json> <比較對象.json> [--box-tolerance 2]

有任何不符時結束代碼為 1。只用 Python 標準函式庫。
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def box_error(a: list, b: list) -> int:
    """兩個四邊形對應角之間，x 或 y 座標差距的最大值。"""
    return max(max(abs(pa[0] - pb[0]), abs(pa[1] - pb[1])) for pa, pb in zip(a, b))


def compare(reference: dict, candidate: dict, box_tolerance: int) -> tuple[list[str], dict]:
    problems: list[str] = []
    stats = {"images": 0, "lines": 0, "max_box_error": 0, "max_score_diff": 0.0}
    candidates = {image["image"]: image for image in candidate["images"]}
    for ref in reference["images"]:
        name = ref["image"]
        stats["images"] += 1
        cand = candidates.get(name)
        if cand is None:
            problems.append(f"{name}: missing from candidate")
            continue
        if (ref["width"], ref["height"]) != (cand["width"], cand["height"]):
            problems.append(f"{name}: image size differs")
        if cand.get("deterministic") is False:
            problems.append(f"{name}: candidate results differ between repeated runs")
        ref_lines, cand_lines = ref["lines"], cand["lines"]
        if len(ref_lines) != len(cand_lines):
            problems.append(f"{name}: {len(ref_lines)} lines vs {len(cand_lines)} lines")
        for i, (a, b) in enumerate(zip(ref_lines, cand_lines)):
            stats["lines"] += 1
            error = box_error(a["box"], b["box"])
            stats["max_box_error"] = max(stats["max_box_error"], error)
            stats["max_score_diff"] = max(stats["max_score_diff"], abs(a["score"] - b["score"]))
            if a["text"] != b["text"]:
                problems.append(f"{name} line {i}: text {a['text']!r} vs {b['text']!r}")
            if error > box_tolerance:
                problems.append(f"{name} line {i}: box {a['box']} vs {b['box']} "
                                f"(off by {error}px)")
    return problems, stats


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--box-tolerance", type=int, default=2)
    args = parser.parse_args()

    reference, candidate = load(args.reference), load(args.candidate)
    problems, stats = compare(reference, candidate, args.box_tolerance)
    print(f"reference: {reference['implementation']} ({reference['device']}), "
          f"candidate: {candidate['implementation']} ({candidate['device']})")
    print(f"{stats['images']} images, {stats['lines']} lines, "
          f"max box error {stats['max_box_error']}px, "
          f"max score difference {stats['max_score_diff']:.6f}")
    if problems:
        print(f"\n{len(problems)} problem(s):")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print("PASS: identical text, boxes within tolerance")
    return 0


if __name__ == "__main__":
    sys.exit(main())
