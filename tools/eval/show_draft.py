"""印出某一頁的標註草稿（給人工修正時參考）。

    python tools/eval/show_draft.py ja-manga 2
"""

from __future__ import annotations

import sys
from pathlib import Path

import ground_truth as gt

REPO_ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    category, number = sys.argv[1], int(sys.argv[2])
    page = gt.load(REPO_ROOT / "build" / "gt_drafts" / category / "ground_truth.draft.txt")[number - 1]
    print(f"== {page.image}  配對: {page.pair}")
    for b in page.blocks:
        ruby = " ".join(f"{r.base}={r.reading}" for r in b.ruby)
        print(f"{b.kind} {b.direction[0]} {b.size} {','.join(map(str, b.box))} | "
              f"{' / '.join(b.lines)} | {ruby} | {b.note}")


if __name__ == "__main__":
    main()
