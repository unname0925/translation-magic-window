"""M0-12：翻譯盲評的視窗。一次看一段原文和各引擎的譯文（打亂、不顯示引擎名稱），逐項打分。

    tools/eval/.venv/Scripts/python tools/eval/rate_translations.py

- 上面是截圖中這個區塊的位置（紅框），下面是原文和 A、B、C… 的譯文。
- 每一列按 1～5 打分（1 完全錯、2 意思有誤、3 意思對但不自然、4 好、5 很好），
  打完會跳到下一列；Enter 或 → 下一項，← 上一項，Ctrl+N 跳到第一個還沒打分的項目。
- 每個項目的譯文順序都不一樣，而且不顯示是哪個引擎，避免先入為主。
- 每打一次分就存檔（build/translation_eval/m0-12/ratings.json），可以隨時關掉再繼續。

評分完用 evaluate_translation.py 產生報告（對照回引擎名稱）。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import random
import tkinter as tk
from dataclasses import dataclass, field
from pathlib import Path
from tkinter import ttk

import ground_truth as gt
from translate import CATEGORIES, OUTPUT, PRIVATE

RATINGS = OUTPUT / "ratings.json"
PER_CATEGORY = 6  # 每個分類抽幾段
MARGIN = 120  # 截圖裁切時區塊周圍留的像素
SCORES = {1: "1 完全錯", 2: "2 意思有誤", 3: "3 意思對但不自然", 4: "4 好", 5: "5 很好"}


@dataclass
class Item:
    key: str
    category: str
    image: str
    index: int
    kind: str
    source: str
    order: list[str]  # 打亂後的引擎順序
    translations: dict[str, str]
    scores: dict[str, int] = field(default_factory=dict)
    note: str = ""


def load_engines(names: list[str] | None) -> dict[str, dict[str, dict]]:
    """引擎 → key → 該段的結果。"""
    engines = {}
    for path in sorted(OUTPUT.glob("*.json")):
        if path.name == RATINGS.name:
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        if names and data["engine"] not in names:
            continue
        engines[data["engine"]] = {row["key"]: row for row in data["segments"]}
    return engines


def sample_keys(engines: dict[str, dict[str, dict]], per_category: int) -> list[str]:
    """每個分類抽固定數量（依 key 的雜湊排序，結果不會變），含有特殊ルビ標記的一定抽到。"""
    common = set.intersection(*(set(rows) for rows in engines.values())) if engines else set()
    usable = {key for key in common
              if all(rows[key]["translation"].strip() for rows in engines.values())}
    any_engine = next(iter(engines.values()))
    keys: list[str] = []
    for category in CATEGORIES:
        in_category = [k for k in usable if k.startswith(f"{category}/")]
        marked = [k for k in in_category if "{" in any_engine[k]["source"]]
        rest = sorted(set(in_category) - set(marked),
                      key=lambda k: hashlib.sha256(k.encode()).hexdigest())
        keys += sorted(marked) + rest[:max(0, per_category - len(marked))]
    return keys


def build_items(engines: dict[str, dict[str, dict]], keys: list[str]) -> list[Item]:
    items = []
    for key in keys:
        row = next(rows[key] for rows in engines.values())
        order = sorted(engines)
        random.Random(key).shuffle(order)  # 每一段的順序不同，但每次執行都一樣
        items.append(Item(key, row["category"], row["image"], row["index"], row["kind"],
                          row["source"], order,
                          {name: rows[key]["translation"] for name, rows in engines.items()}))
    return items


def load_ratings(items: list[Item], path: Path = RATINGS) -> None:
    if not path.exists():
        return
    saved = {row["key"]: row for row in json.loads(path.read_text(encoding="utf-8"))["items"]}
    for item in items:
        row = saved.get(item.key)
        if row and row["source"] == item.source:
            item.scores = {k: v for k, v in row["scores"].items() if k in item.translations}
            item.note = row.get("note", "")


def save_ratings(items: list[Item], path: Path = RATINGS) -> None:
    data = {"items": [{"key": i.key, "category": i.category, "source": i.source,
                       "scores": i.scores, "note": i.note} for i in items if i.scores or i.note]}
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(data, ensure_ascii=False, indent=1), encoding="utf-8",
                         newline="\n")
    temporary.replace(path)


def crop_around(category: str, image: str, box: tuple[int, int, int, int],
                size: tuple[int, int]):
    """截圖中區塊附近的畫面，區塊畫上紅框，縮到 size 以內。"""
    from PIL import Image, ImageDraw

    picture = Image.open(PRIVATE / category / image).convert("RGB")
    draw = ImageDraw.Draw(picture)
    draw.rectangle(box, outline=(230, 0, 0), width=max(2, picture.width // 500))
    area = (max(0, box[0] - MARGIN), max(0, box[1] - MARGIN),
            min(picture.width, box[2] + MARGIN), min(picture.height, box[3] + MARGIN))
    picture = picture.crop(area)
    picture.thumbnail(size, Image.Resampling.LANCZOS)
    return picture


def block_boxes(category: str) -> dict[tuple[str, int], tuple[int, int, int, int]]:
    boxes = {}
    for page in gt.load(PRIVATE / category / "ground_truth.txt"):
        for i, block in enumerate(page.blocks, start=1):
            boxes[page.image, i] = block.box
    return boxes


class App:
    def __init__(self, items: list[Item], path: Path = RATINGS):
        self.items = items
        self.path = path
        self.position = next((i for i, x in enumerate(items) if len(x.scores) < len(x.order)), 0)
        from PIL import ImageTk

        self.to_photo = ImageTk.PhotoImage
        self.boxes = {c: block_boxes(c) for c in {i.category for i in items}}
        self.row = 0
        self.photo = None

        self.root = tk.Tk()
        self.root.title("翻譯盲評")
        self.root.state("zoomed")
        self.scale = self.root.tk.call("tk", "scaling")
        self.header = ttk.Label(self.root, font=("Microsoft JhengHei", 11))
        self.header.pack(anchor="w", padx=10, pady=(8, 2))
        self.picture = ttk.Label(self.root)
        self.picture.pack(pady=4)
        self.source = tk.Text(self.root, height=3, wrap="word", font=("Microsoft JhengHei", 13))
        self.source.pack(fill="x", padx=10, pady=4)
        self.rows = ttk.Frame(self.root)
        self.rows.pack(fill="x", padx=10, pady=4)
        self.labels: list[ttk.Label] = []
        self.texts: list[tk.Text] = []
        self.buttons: list[list[ttk.Button]] = []
        for i in range(8):  # 最多 8 個引擎
            label = ttk.Label(self.rows, font=("Microsoft JhengHei", 12, "bold"), width=3)
            text = tk.Text(self.rows, height=2, wrap="word", font=("Microsoft JhengHei", 13))
            buttons = ttk.Frame(self.rows)
            row = [ttk.Button(buttons, text=str(s), width=3,
                              command=lambda s=s, i=i: self.rate(i, s)) for s in SCORES]
            for button in row:
                button.pack(side="left", padx=1)
            text.bind("<Button-1>", lambda event, i=i: self.select_row(i))
            label.bind("<Button-1>", lambda event, i=i: self.select_row(i))
            self.labels.append(label)
            self.texts.append(text)
            self.buttons.append(row)
            label.grid(row=i, column=0, sticky="n", pady=3)
            text.grid(row=i, column=1, sticky="ew", pady=3)
            buttons.grid(row=i, column=2, sticky="n", padx=8, pady=3)
        self.rows.columnconfigure(1, weight=1)
        self.note = ttk.Entry(self.root, font=("Microsoft JhengHei", 11))
        self.note.pack(fill="x", padx=10, pady=4)
        self.note.bind("<KeyRelease>", self.on_note)
        ttk.Label(self.root, text="1～5 打分（" + "、".join(SCORES.values()) + "）　"
                                 "Enter／→ 下一項　← 上一項　↑↓ 或點譯文選要打分的那一列　"
                                 "Ctrl+N 跳到還沒打分的",
                  font=("Microsoft JhengHei", 10)).pack(anchor="w", padx=10, pady=(0, 8))
        for score in SCORES:
            self.root.bind(str(score), lambda event, s=score: self.rate(self.row, s))
        self.root.bind("<Return>", lambda event: self.go(1))
        self.root.bind("<Right>", lambda event: self.go(1))
        self.root.bind("<Left>", lambda event: self.go(-1))
        self.root.bind("<Control-n>", lambda event: self.go_unrated())
        self.root.bind("<Down>", lambda event: self.select_row(self.row + 1))
        self.root.bind("<Up>", lambda event: self.select_row(self.row - 1))
        self.root.bind("<Escape>", lambda event: self.root.destroy())
        self.show()

    # ------------------------------------------------------------------------------------
    def item(self) -> Item:
        return self.items[self.position]

    def show(self) -> None:
        item = self.item()
        done = sum(1 for x in self.items if len(x.scores) == len(x.order))
        self.header.config(text=f"第 {self.position + 1} / {len(self.items)} 項　"
                                f"已完成 {done}　{item.category}　{item.kind}")
        box = self.boxes[item.category][item.image, item.index]
        width = int(self.root.winfo_screenwidth() * 0.55)
        height = int(self.root.winfo_screenheight() * 0.34)
        self.photo = self.to_photo(crop_around(item.category, item.image, box,
                                               (width, height)))
        self.picture.config(image=self.photo)
        self.set_text(self.source, item.source)
        for i in range(len(self.labels)):
            if i < len(item.order):
                engine = item.order[i]
                self.labels[i].grid()
                self.texts[i].grid()
                self.buttons[i][0].master.grid()
                self.labels[i].config(text=chr(ord("A") + i))
                self.set_text(self.texts[i], item.translations[engine])
            else:
                self.labels[i].grid_remove()
                self.texts[i].grid_remove()
                self.buttons[i][0].master.grid_remove()
        self.note.delete(0, "end")
        self.note.insert(0, item.note)
        self.row = next((i for i in range(len(item.order))
                         if item.order[i] not in item.scores), 0)
        self.highlight()

    def set_text(self, widget: tk.Text, value: str) -> None:
        widget.config(state="normal")
        widget.delete("1.0", "end")
        widget.insert("1.0", value)
        widget.config(state="disabled")

    def highlight(self) -> None:
        item = self.item()
        for i in range(len(item.order)):
            score = item.scores.get(item.order[i])
            self.labels[i].config(text=f"{chr(ord('A') + i)}{'' if score is None else f' {score}'}")
            self.texts[i].config(background="#fff7d6" if i == self.row else "white")

    def rate(self, row: int, score: int) -> None:
        item = self.item()
        if row >= len(item.order):
            return
        was_complete = len(item.scores) == len(item.order)  # 重打分數時不要跳走
        item.scores[item.order[row]] = score
        save_ratings(self.items, self.path)
        self.row = min(row + 1, len(item.order) - 1)
        self.highlight()
        if not was_complete and len(item.scores) == len(item.order)                 and self.position + 1 < len(self.items):
            self.root.after(150, lambda: self.go(1))

    def select_row(self, row: int) -> None:
        self.row = max(0, min(len(self.item().order) - 1, row))
        self.highlight()

    def on_note(self, _event) -> None:
        self.item().note = self.note.get()
        save_ratings(self.items, self.path)

    def go(self, step: int) -> None:
        self.position = max(0, min(len(self.items) - 1, self.position + step))
        self.show()

    def go_unrated(self) -> None:
        nxt = next((i for i, x in enumerate(self.items) if len(x.scores) < len(x.order)), None)
        if nxt is not None:
            self.position = nxt
            self.show()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--engines", nargs="+", help="只評這些引擎（預設全部）")
    parser.add_argument("--per-category", type=int, default=PER_CATEGORY)
    args = parser.parse_args()

    try:  # 和 gt_editor 一樣，在高 DPI 螢幕上不要被系統放大成模糊的畫面
        import ctypes
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except Exception:  # noqa: BLE001 - 沒有這個 API 的環境就照舊
        pass
    engines = load_engines(args.engines)
    if not engines:
        raise SystemExit(f"{OUTPUT} 裡沒有翻譯結果，先跑 translate.py")
    items = build_items(engines, sample_keys(engines, args.per_category))
    load_ratings(items)
    print(f"{len(engines)} 個引擎、{len(items)} 段")
    App(items).root.mainloop()


if __name__ == "__main__":
    main()
