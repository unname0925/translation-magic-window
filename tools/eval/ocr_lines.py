"""OCR 結果的逐行處理：判斷ルビ、排出閱讀順序。只用標準函式庫。

草稿（draft_ground_truth.py）、編輯器（gt_editor.py）和評測（evaluate_ocr.py）共用，
規則見 design.md 4.4（ルビ是比旁邊本文細很多、貼在右側或上方的行）。輸入是 tmw_ocr_cli 輸出的行：{"text", "score", "box": 四個角}。
"""

from __future__ import annotations

from dataclasses import dataclass, field

RUBY_RATIO = 0.65


@dataclass
class Line:
    index: int
    text: str
    score: float
    x0: int
    y0: int
    x1: int
    y1: int
    vertical: bool
    ruby_of: int | None = None
    rubies: list[int] = field(default_factory=list)

    @property
    def thickness(self) -> int:
        return (self.x1 - self.x0) if self.vertical else (self.y1 - self.y0)

    @property
    def length(self) -> int:
        return (self.y1 - self.y0) if self.vertical else (self.x1 - self.x0)


def to_lines(ocr_lines: list[dict]) -> list[Line]:
    lines = []
    for i, item in enumerate(ocr_lines):
        if not item["text"].strip():
            continue  # 辨識不出任何字的框
        xs = [p[0] for p in item["box"]]
        ys = [p[1] for p in item["box"]]
        w, h = max(xs) - min(xs), max(ys) - min(ys)
        lines.append(Line(i, item["text"], item["score"], min(xs), min(ys), max(xs), max(ys),
                          vertical=h > w * 1.2 and len(item["text"]) > 1))
    return lines


def overlap(a0: int, a1: int, b0: int, b1: int) -> int:
    return max(0, min(a1, b1) - max(a0, b0))


def find_ruby(lines: list[Line]) -> None:
    """把細的行對應到它旁邊的本文。"""
    for ruby in lines:
        best, best_gap = None, None
        for base in lines:
            if base is ruby or base.vertical != ruby.vertical:
                continue
            if ruby.thickness >= base.thickness * RUBY_RATIO:
                continue
            if base.vertical:
                gap = ruby.x0 - base.x1  # ルビ在本文右側
                along = overlap(ruby.y0, ruby.y1, base.y0, base.y1)
            else:
                gap = base.y0 - ruby.y1  # ルビ在本文上方
                along = overlap(ruby.x0, ruby.x1, base.x0, base.x1)
            if -base.thickness * 0.3 <= gap <= base.thickness * 0.5 and along >= ruby.length * 0.6:
                if best_gap is None or abs(gap) < best_gap:
                    best, best_gap = base, abs(gap)
        if best is not None:
            ruby.ruby_of = best.index
            best.rubies.append(ruby.index)



def order_ocr_lines(lines: list[dict], direction: str, language: str) -> list[str]:
    """把 OCR 的結果排成閱讀順序的行。

    橫排：同一列（垂直方向重疊）的框是同一行，由上到下；行內由左到右。
    直排：同一欄的框是同一行，由右到左；欄內由上到下。
    韓文和英文的偵測框常常一個詞一個框，同一行的框用空白相接；日文直接相接。
    """
    items = []
    for line in lines:
        text = line["text"].strip()
        if text:
            xs = [p[0] for p in line["box"]]
            ys = [p[1] for p in line["box"]]
            items.append((min(xs), min(ys), max(xs), max(ys), text))
    vertical = direction == "vertical"
    low, high = (0, 2) if vertical else (1, 3)  # 分行的座標
    along = 1 if vertical else 0  # 行內排序的座標
    groups: list[list] = []  # [low, high, 框]
    for item in sorted(items, key=lambda it: (it[low] + it[high]) / 2, reverse=vertical):
        center = (item[low] + item[high]) / 2
        if groups and groups[-1][0] <= center <= groups[-1][1]:
            group = groups[-1]
            group[0], group[1] = min(group[0], item[low]), max(group[1], item[high])
            group[2].append(item)
        else:
            groups.append([item[low], item[high], [item]])
    separator = " " if language in ("en", "ko") else ""
    return [separator.join(it[4] for it in sorted(group[2], key=lambda it: it[along]))
            for group in groups]
