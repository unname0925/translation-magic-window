"""M0-10：正確答案編輯器。直接在截圖上框選有字的地方、輸入文字，存回 ground_truth.txt。

    tools/eval/.venv/Scripts/python tools/eval/gt_editor.py [分類]

操作：
- 圖上：在空白處拖曳新增一個框（按住 Shift 可以在別的框裡面新增）；點框選取；拖曳選取的框
  移動它，拖曳角或邊調整大小。滾輪縮放，右鍵拖曳移動畫面。
- 右邊：區塊清單的順序就是閱讀順序（用「上移」「下移」調整）；下面編輯選取的區塊。
- 快捷鍵：Ctrl+S 存檔、Ctrl+R 用 OCR 辨識選取的框、Ctrl+Z 復原框的變更；
  焦點在圖上時（先點一下圖）：Delete 刪除選取的框、PageUp／PageDown 換張、Esc 取消選取。

存檔前會檢查格式，有問題會跳到那個區塊；第一次存檔時把原本的檔案備份成 ground_truth.txt.bak。
Ctrl+R 呼叫 C++ 的 tmw_ocr_cli（先執行 cmake --workflow --preset release，並下載模型）。
「匯入草稿」把 build/gt_drafts 裡這張截圖的草稿加進來（給新加的截圖用，見 README）。
"""

from __future__ import annotations

import argparse
import copy
import json
import math
import queue
import shutil
import subprocess
import tempfile
import threading
import tkinter as tk
from dataclasses import dataclass, field
from pathlib import Path
from tkinter import messagebox, ttk
from typing import TYPE_CHECKING

import ground_truth as gt
from ocr_lines import order_ocr_lines

if TYPE_CHECKING:
    from PIL import Image

REPO_ROOT = Path(__file__).resolve().parents[2]
PRIVATE = REPO_ROOT / "testdata" / "private"
DRAFTS = REPO_ROOT / "build" / "gt_drafts"
OCR_CLI = REPO_ROOT / "build" / "msvc-x64" / "bin" / "Release" / "tmw_ocr_cli.exe"
MODELS = REPO_ROOT / "models"
IMAGE_SUFFIXES = (".png", ".jpg", ".jpeg", ".webp")
DEFAULT_HEADER = ["# 正確答案（M0-10）。格式說明見 tools/eval/ground_truth.py。"]
EXCLUDE_PREFIX = "不評測："

Box = tuple[int, int, int, int]


# ---------------------------------------------------------------------------------------------
# 文件：讀取、檢查、存檔（不需要視窗，有單元測試）

@dataclass
class Document:
    category: str
    path: Path
    header: list[str] = field(default_factory=list)  # 檔案開頭的註解，存檔時原樣保留
    pages: list[gt.Page] = field(default_factory=list)


def image_files(folder: Path) -> list[str]:
    return sorted(p.name for p in folder.iterdir()
                  if p.is_file() and p.suffix.lower() in IMAGE_SUFFIXES)


def split_header(text: str) -> tuple[list[str], str]:
    """分成檔案開頭（第一張截圖之前）的註解，和其餘的內容。"""
    lines = text.splitlines()
    first = next((i for i, line in enumerate(lines) if line.startswith("== ")), len(lines))
    header = lines[:first]
    while header and not header[-1].strip():
        header.pop()
    body = "\n".join(lines[first:])
    return header, body + "\n" if body else ""


def load_document(category: str, root: Path = PRIVATE) -> Document:
    """讀取正確答案；資料夾裡還沒標註的截圖加在最後（沒有區塊）。"""
    folder = root / category
    path = folder / "ground_truth.txt"
    if path.exists():
        header, body = split_header(path.read_text(encoding="utf-8-sig"))
    else:
        header, body = list(DEFAULT_HEADER), ""
    pages = gt.parse(body)
    known = {page.image for page in pages}
    pages += [gt.Page(image=name) for name in image_files(folder) if name not in known]
    return Document(category, path, header, pages)


def document_text(doc: Document) -> str:
    header = "\n".join(doc.header)
    return (header + "\n\n" if header else "") + gt.dump(doc.pages)


def parse_ruby(text: str) -> list[gt.Ruby]:
    """「本文=讀音 本文=讀音!」（和檔案的ルビ欄位相同）。"""
    result = []
    for item in text.split():
        base, separator, reading = item.partition("=")
        if not separator or not base or not reading.rstrip("!"):
            raise ValueError(f"「{item}」要寫成 本文=讀音（另有含義的加 !）")
        result.append(gt.Ruby(base, reading.rstrip("!"), reading.endswith("!")))
    return result


def format_ruby(ruby: list[gt.Ruby]) -> str:
    return " ".join(f"{r.base}={r.reading}{'!' if r.meaning else ''}" for r in ruby)


def block_problems(block: gt.Block) -> list[str]:
    problems = []
    x0, y0, x1, y1 = block.box
    if x1 <= x0 or y1 <= y0:
        problems.append("框是空的")
    if not block.lines:
        problems.append("沒有文字")
    for line in block.lines:
        if gt.is_markup(line):
            problems.append(f"「{line}」會被當成格式，不能當作文字")
    joined = "".join(block.lines)
    position = 0
    for ruby in block.ruby:
        found = joined.find(ruby.base, position)
        if found < 0:
            problems.append(f"ルビ的本文「{ruby.base}」依序找不到（ルビ要照出現的順序寫）")
            continue
        position = found + len(ruby.base)
    return problems


def document_problems(doc: Document) -> list[tuple[int, int, str]]:
    """(第幾張, 第幾個區塊, 問題)，從 0 起算；區塊是 -1 表示不是單一區塊的問題。"""
    problems = [(p, b, problem)
                for p, page in enumerate(doc.pages)
                for b, block in enumerate(page.blocks)
                for problem in block_problems(block)]
    if not problems:
        try:
            if gt.parse(document_text(doc)) != doc.pages:
                problems.append((-1, -1, "存檔後讀回來的內容和編輯的不一樣"))
        except gt.FormatError as error:
            problems.append((-1, -1, str(error)))
    return problems


def save_document(doc: Document, backup: bool) -> None:
    """存檔（換行一律用 LF）。backup 為 True 時先把原本的檔案備份成 ground_truth.txt.bak。"""
    text = document_text(doc)
    if backup and doc.path.exists():
        shutil.copyfile(doc.path, doc.path.with_name(doc.path.name + ".bak"))
    temporary = doc.path.with_name(doc.path.name + ".tmp")
    temporary.write_text(text, encoding="utf-8", newline="\n")
    temporary.replace(doc.path)


def draft_blocks(category: str, image: str, drafts: Path = DRAFTS) -> list[gt.Block]:
    path = drafts / category / "ground_truth.draft.txt"
    if not path.exists():
        return []
    for page in gt.load(path):
        if page.image == image:
            return copy.deepcopy(page.blocks)
    return []


# ---------------------------------------------------------------------------------------------
# 框的幾何（截圖座標）

def normalize_box(x0: float, y0: float, x1: float, y1: float, width: int, height: int) -> Box:
    """左上、右下排好，並限制在截圖範圍內。"""
    left, right = sorted((x0, x1))
    top, bottom = sorted((y0, y1))

    def clamp(value: float, limit: int) -> int:
        return max(0, min(limit, round(value)))

    return clamp(left, width), clamp(top, height), clamp(right, width), clamp(bottom, height)


def move_box(box: Box, dx: float, dy: float, width: int, height: int) -> Box:
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    left = max(0, min(width - w, round(x0 + dx)))
    top = max(0, min(height - h, round(y0 + dy)))
    return left, top, left + w, top + h


def hit_handle(box: Box, x: float, y: float, tolerance: float) -> str:
    """(x, y) 靠近框的哪個角或邊：n、s、e、w 的組合（例如 "nw"）；都不靠近時是空字串。"""
    x0, y0, x1, y1 = box
    if not (x0 - tolerance <= x <= x1 + tolerance and y0 - tolerance <= y <= y1 + tolerance):
        return ""
    vertical = "n" if abs(y - y0) <= tolerance else "s" if abs(y - y1) <= tolerance else ""
    horizontal = "w" if abs(x - x0) <= tolerance else "e" if abs(x - x1) <= tolerance else ""
    return vertical + horizontal


def resize_box(box: Box, handle: str, dx: float, dy: float, width: int, height: int) -> Box:
    """把 handle 指的角或邊移動 (dx, dy)。"""
    x0, y0, x1, y1 = box
    if "n" in handle:
        y0 += dy
    if "s" in handle:
        y1 += dy
    if "w" in handle:
        x0 += dx
    if "e" in handle:
        x1 += dx
    return normalize_box(x0, y0, x1, y1, width, height)


def block_at(blocks: list[gt.Block], x: float, y: float) -> int | None:
    """包含 (x, y) 的框裡面積最小的一個（框裡面的小框也點得到）。"""
    best, best_area = None, 0
    for i, block in enumerate(blocks):
        x0, y0, x1, y1 = block.box
        if x0 <= x <= x1 and y0 <= y <= y1:
            area = (x1 - x0) * (y1 - y0)
            if best is None or area < best_area:
                best, best_area = i, area
    return best


def guess_direction(box: Box) -> str:
    x0, y0, x1, y1 = box
    return "vertical" if (y1 - y0) > (x1 - x0) * 1.5 else "horizontal"


# ---------------------------------------------------------------------------------------------
# OCR：用 C++ 的 tmw_ocr_cli 辨識框裡的字

def ocr_models(language: str) -> tuple[Path, Path]:
    if language == "ko":
        return MODELS / "PP-OCRv5_server_det", MODELS / "korean_PP-OCRv5_mobile_rec"
    return MODELS / "PP-OCRv6_medium_det", MODELS / "PP-OCRv6_medium_rec"


def recognize_box(image: Image.Image, box: Box, language: str, direction: str,
                  cli: Path = OCR_CLI) -> list[str]:
    if not cli.exists():
        raise RuntimeError(f"找不到 {cli}\n先執行 cmake --workflow --preset release")
    detection, recognition = ocr_models(language)
    if not recognition.exists():
        raise RuntimeError(f"找不到模型 {recognition}\n先執行 python tools/fetch_models/fetch_models.py")
    margin = 6
    x0, y0, x1, y1 = box
    crop = (max(0, x0 - margin), max(0, y0 - margin),
            min(image.width, x1 + margin), min(image.height, y1 + margin))
    with tempfile.TemporaryDirectory() as folder:
        crop_path = Path(folder) / "crop.png"
        output = Path(folder) / "result.json"
        image.crop(crop).save(crop_path)
        result = subprocess.run(
            [str(cli), "--det", str(detection), "--rec", str(recognition), "--device", "dml",
             "--output", str(output), str(crop_path)],
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        if result.returncode != 0:
            raise RuntimeError(result.stderr.strip() or f"tmw_ocr_cli 結束代碼 {result.returncode}")
        lines = json.loads(output.read_text(encoding="utf-8"))["images"][0]["lines"]
    return order_ocr_lines(lines, direction, language)


# ---------------------------------------------------------------------------------------------
# 視窗

EVALUATED = "#e60000"
EXCLUDED = "#8a8a8a"
SELECTED = "#1e6bff"
UI_FONT = ("Microsoft JhengHei UI", 10)
TEXT_FONT = ("Microsoft JhengHei UI", 13)
LABEL_FONT = ("Microsoft JhengHei UI", 9, "bold")
HANDLE = 6  # 角和邊可以拖曳的範圍（螢幕像素）
ZOOM_STEP = 1.25
MIN_ZOOM, MAX_ZOOM = 0.05, 8.0
CURSORS = {"nw": "size_nw_se", "se": "size_nw_se", "ne": "size_ne_sw", "sw": "size_ne_sw",
           "n": "sb_v_double_arrow", "s": "sb_v_double_arrow",
           "e": "sb_h_double_arrow", "w": "sb_h_double_arrow"}
HELP = ("圖上：拖曳空白處新增框（Shift＋拖曳可以在框裡面新增），拖曳框移動，拖曳角或邊調整大小；"
        "滾輪縮放，右鍵拖曳移動畫面。\n"
        "Ctrl+S 存檔　Ctrl+R 辨識選取的框　Ctrl+Z 復原框的變更\n"
        "先點一下圖：Delete 刪除框　PageUp／PageDown 換張　Esc 取消選取\n"
        "ルビ：本文=讀音，另有含義的加 !（例如 本気=マジ!）　參考：另一種語言版本的譯文\n"
        "語言：和整個分類不同時才填（例如 ja）　位置：x0,y0,x1,y1，改完按 Enter")


class App:
    def __init__(self, root: tk.Tk, category: str | None) -> None:
        from PIL import Image, ImageTk
        self.pil_image, self.pil_tk = Image, ImageTk
        self.root = root
        self.doc: Document | None = None
        self.page_index = 0
        self.selected: int | None = None
        self.dirty = False
        self.backed_up = False
        self.undo: list[tuple[int, list[gt.Block]]] = []
        self.image: Image.Image | None = None
        self.photo = None
        self.scale = 1.0
        self.origin = (0.0, 0.0)  # 畫面左上角對應的截圖座標
        self.drag: dict | None = None
        self.pan: tuple | None = None
        self.last_kind = "對白"
        self.loading = False  # 把區塊填進欄位時，不要當成使用者的修改
        self.ruby_error = ""
        self.ocr_busy = False
        self.render_pending = False
        self.auto_fit = True  # 還沒縮放或移動過：視窗大小改變時重新符合視窗
        self.cursor = ""
        self._build()
        categories = sorted(p.name for p in PRIVATE.iterdir() if p.is_dir())
        self.category_box["values"] = categories
        if category and category not in categories:
            messagebox.showerror("找不到分類", f"testdata/private 裡沒有 {category}")
            category = None
        if categories:
            self.open_category(category or categories[0])

    # ---- 版面

    def _build(self) -> None:
        root = self.root
        root.geometry("1600x960")
        root.state("zoomed")  # 最大化：右邊的欄位才放得下
        root.option_add("*Font", UI_FONT)
        ttk.Style().configure(".", font=UI_FONT)

        top = ttk.Frame(root, padding=4)
        top.pack(side="top", fill="x")
        ttk.Label(top, text="分類").pack(side="left")
        self.category_var = tk.StringVar()
        self.category_box = ttk.Combobox(top, textvariable=self.category_var, state="readonly",
                                         width=10)
        self.category_box.pack(side="left", padx=(4, 12))
        self.category_box.bind("<<ComboboxSelected>>",
                               lambda _: self.open_category(self.category_var.get()))
        ttk.Button(top, text="◀ 上一張",
                   command=lambda: self.go_page(self.page_index - 1)).pack(side="left")
        self.page_box = ttk.Combobox(top, state="readonly", width=52)
        self.page_box.pack(side="left", padx=4)
        self.page_box.bind("<<ComboboxSelected>>", lambda _: self.go_page(self.page_box.current()))
        ttk.Button(top, text="下一張 ▶",
                   command=lambda: self.go_page(self.page_index + 1)).pack(side="left")
        ttk.Button(top, text="符合視窗", command=self.fit).pack(side="left", padx=(16, 4))
        ttk.Button(top, text="匯入草稿", command=self.import_draft).pack(side="left", padx=4)
        ttk.Button(top, text="存檔", command=self.save).pack(side="left", padx=4)
        self.zoom_label = ttk.Label(top, width=8, anchor="e")
        self.zoom_label.pack(side="right")

        bottom = ttk.Frame(root, padding=(6, 2))
        bottom.pack(side="bottom", fill="x")
        self.status = tk.StringVar()
        ttk.Label(bottom, textvariable=self.status, anchor="w").pack(side="left", fill="x",
                                                                     expand=True)
        self.position = tk.StringVar()
        ttk.Label(bottom, textvariable=self.position, width=16, anchor="e").pack(side="right")

        paned = ttk.PanedWindow(root, orient="horizontal")
        paned.pack(fill="both", expand=True)
        self.canvas = tk.Canvas(paned, background="#3c3c3c", highlightthickness=0,
                                cursor="crosshair", takefocus=True)
        paned.add(self.canvas, weight=1)
        # 右邊欄的寬度依螢幕縮放決定（tk scaling 是每點幾個像素，150% 縮放時是 2）
        side_width = round(340 * float(root.tk.call("tk", "scaling")))
        side = ttk.Frame(paned, padding=6, width=side_width)
        side.pack_propagate(False)
        paned.add(side, weight=0)
        self._build_side(side, side_width - 24)

        canvas = self.canvas
        canvas.bind("<ButtonPress-1>", self.on_press)
        canvas.bind("<B1-Motion>", self.on_drag)
        canvas.bind("<ButtonRelease-1>", self.on_release)
        canvas.bind("<ButtonPress-3>", self.on_pan_start)
        canvas.bind("<B3-Motion>", self.on_pan)
        canvas.bind("<MouseWheel>", self.on_wheel)
        canvas.bind("<Motion>", self.on_motion)
        canvas.bind("<Configure>", lambda _: self.fit() if self.auto_fit else self.schedule_render())
        canvas.bind("<Delete>", lambda _: self.delete_block())
        canvas.bind("<Prior>", lambda _: self.go_page(self.page_index - 1))
        canvas.bind("<Next>", lambda _: self.go_page(self.page_index + 1))
        canvas.bind("<Escape>", lambda _: self.select(None))
        root.bind_all("<Control-s>", lambda _: self.save())
        root.bind_all("<Control-r>", lambda _: self.recognize())
        root.bind_all("<Control-z>", self.on_undo_key)
        root.protocol("WM_DELETE_WINDOW", self.on_close)

    def _build_side(self, side: ttk.Frame, wrap: int) -> None:
        # 下方的編輯欄位和說明先放，視窗不夠高時縮小的是上面的區塊清單
        ttk.Label(side, text=HELP, foreground="#555", wraplength=wrap,
                  justify="left").pack(side="bottom", anchor="w", pady=(8, 0))
        editor = ttk.LabelFrame(side, text="選取的區塊", padding=6)
        editor.pack(side="bottom", fill="x")
        buttons = ttk.Frame(side)
        buttons.pack(side="bottom", fill="x", pady=4)
        ttk.Button(buttons, text="上移", width=5,
                   command=lambda: self.move_block(-1)).pack(side="left")
        ttk.Button(buttons, text="下移", width=5,
                   command=lambda: self.move_block(1)).pack(side="left", padx=4)
        ttk.Button(buttons, text="刪除", width=5, command=self.delete_block).pack(side="left")
        ttk.Button(buttons, text="辨識文字 (Ctrl+R)",
                   command=self.recognize).pack(side="right")
        ttk.Label(side, text="區塊（清單的順序就是閱讀順序）").pack(side="top", anchor="w")
        list_frame = ttk.Frame(side)
        list_frame.pack(side="top", fill="both", expand=True)
        self.block_list = tk.Listbox(list_frame, activestyle="none", exportselection=False,
                                     height=6)
        scroll = ttk.Scrollbar(list_frame, command=self.block_list.yview)
        self.block_list.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.block_list.pack(side="left", fill="both", expand=True)
        self.block_list.bind("<<ListboxSelect>>", self.on_list_select)

        editor.columnconfigure(1, weight=1)
        self.editor = editor
        self.kind_var = tk.StringVar()
        self.direction_var = tk.StringVar()
        self.size_var = tk.StringVar()
        self.ruby_var = tk.StringVar()
        self.reference_var = tk.StringVar()
        self.language_var = tk.StringVar()
        self.note_var = tk.StringVar()
        self.excluded_var = tk.BooleanVar()
        self.box_var = tk.StringVar()

        def label(row: int, text: str) -> None:
            ttk.Label(editor, text=text).grid(row=row, column=0, sticky="nw", padx=(0, 6), pady=2)

        label(0, "種類")
        kind = ttk.Combobox(editor, textvariable=self.kind_var, values=gt.KINDS,
                            state="readonly", width=8)
        kind.grid(row=0, column=1, sticky="w", pady=2)
        label(1, "方向")
        directions = ttk.Frame(editor)
        directions.grid(row=1, column=1, sticky="w")
        for text, value in (("橫", "horizontal"), ("直", "vertical")):
            ttk.Radiobutton(directions, text=text, value=value,
                            variable=self.direction_var).pack(side="left", padx=(0, 10))
        label(2, "字級")
        sizes = ttk.Frame(editor)
        sizes.grid(row=2, column=1, sticky="w")
        for text, value in (("小", "small"), ("中", "normal"), ("大", "large")):
            ttk.Radiobutton(sizes, text=text, value=value,
                            variable=self.size_var).pack(side="left", padx=(0, 10))
        label(3, "文字")
        self.text = tk.Text(editor, height=5, width=36, font=TEXT_FONT, undo=True, wrap="none")
        self.text.grid(row=3, column=1, sticky="ew", pady=2)
        self.text.bind("<<Modified>>", self.on_text_modified)
        entries = []
        for row, (text, variable) in enumerate((
                ("ルビ", self.ruby_var), ("參考", self.reference_var),
                ("語言", self.language_var), ("備註", self.note_var),
                ("位置", self.box_var)), start=4):
            label(row, text)
            entry = ttk.Entry(editor, textvariable=variable)
            entry.grid(row=row, column=1, sticky="ew", pady=2)
            entries.append(entry)
        self.box_entry = entries[-1]
        self.box_entry.bind("<Return>", lambda _: self.apply_box_field())
        self.box_entry.bind("<FocusOut>", lambda _: self.apply_box_field())
        excluded = ttk.Checkbutton(editor, text="不評測（備註開頭加上「不評測：」）",
                                   variable=self.excluded_var)
        excluded.grid(row=9, column=1, sticky="w", pady=2)
        self.problem_label = ttk.Label(editor, foreground="#c00000", wraplength=wrap - 20,
                                       justify="left")
        self.problem_label.grid(row=10, column=0, columnspan=2, sticky="w")
        self.editor_widgets = [kind, *directions.winfo_children(), *sizes.winfo_children(),
                               *entries, excluded]

        for name, variable in (("kind", self.kind_var), ("direction", self.direction_var),
                               ("size", self.size_var), ("ruby", self.ruby_var),
                               ("reference", self.reference_var),
                               ("language", self.language_var), ("note", self.note_var),
                               ("excluded", self.excluded_var)):
            variable.trace_add("write", lambda *_, name=name: self.on_field_changed(name))

    # ---- 文件和頁面

    def page(self) -> gt.Page | None:
        if self.doc is None or not self.doc.pages:
            return None
        return self.doc.pages[self.page_index]

    def open_category(self, category: str) -> None:
        if not self.confirm_discard():
            self.category_var.set(self.doc.category if self.doc else "")
            return
        try:
            doc = load_document(category)
        except gt.FormatError as error:
            messagebox.showerror("無法讀取", f"{category}/ground_truth.txt：{error}")
            self.category_var.set(self.doc.category if self.doc else "")
            return
        self.doc = doc
        self.dirty = False
        self.backed_up = False
        self.undo.clear()
        self.category_var.set(category)
        self.page_index = 0
        self.go_page(0)
        missing = sum(not page.blocks for page in doc.pages)
        self.status.set(f"{category}：{len(doc.pages)} 張" +
                        (f"，其中 {missing} 張還沒有區塊" if missing else ""))

    def go_page(self, index: int) -> None:
        if self.doc is None or not 0 <= index < len(self.doc.pages):
            return
        self.page_index = index
        page = self.page()
        path = PRIVATE / self.doc.category / page.image
        self.image = self.pil_image.open(path).convert("RGB") if path.exists() else None
        if self.image is None:
            self.status.set(f"找不到截圖 {path}")
        self.refresh_pages()
        self.refresh_list()
        self.select(None)
        self.root.update_idletasks()
        self.fit()

    def refresh_pages(self) -> None:
        pages = self.doc.pages
        self.page_box["values"] = [f"{i + 1}. {p.image}（{len(p.blocks)} 個區塊）"
                                   for i, p in enumerate(pages)]
        if pages:
            self.page_box.current(self.page_index)
        self.update_title()

    def update_title(self) -> None:
        title = "正確答案編輯器"
        if self.doc is not None:
            title += f" — {self.doc.category}"
            if self.doc.pages:
                title += f" — {self.page_index + 1}/{len(self.doc.pages)} {self.page().image}"
        self.root.title(title + (" *" if self.dirty else ""))

    def set_dirty(self) -> None:
        if not self.dirty:
            self.dirty = True
            self.update_title()

    def confirm_discard(self) -> bool:
        if not self.dirty:
            return True
        answer = messagebox.askyesnocancel("還沒存檔", "有修改還沒存檔，要先存檔嗎？")
        if answer is None:
            return False
        return self.save() if answer else True

    def save(self) -> bool:
        if self.doc is None:
            return True
        if self.ruby_error:
            messagebox.showerror("無法存檔", f"ルビ：{self.ruby_error}")
            return False
        problems = document_problems(self.doc)
        if problems:
            page, block, message = problems[0]
            where = ""
            if page >= 0:
                if page != self.page_index:
                    self.go_page(page)
                self.select(block)
                where = f"第 {page + 1} 張、區塊 {block + 1}："
            more = f"\n（另外還有 {len(problems) - 1} 個問題）" if len(problems) > 1 else ""
            messagebox.showerror("無法存檔", where + message + more)
            return False
        save_document(self.doc, backup=not self.backed_up)
        self.backed_up = True
        self.dirty = False
        self.update_title()
        self.refresh_pages()
        self.status.set(f"已存檔：{self.doc.path}")
        return True

    def on_close(self) -> None:
        if self.confirm_discard():
            self.root.destroy()

    # ---- 區塊清單和編輯欄位

    def current_block(self) -> gt.Block | None:
        page = self.page()
        return None if page is None or self.selected is None else page.blocks[self.selected]

    def list_item(self, index: int, block: gt.Block) -> str:
        text = block.lines[0] if block.lines else "（沒有文字）"
        if len(text) > 22:
            text = text[:22] + "…"
        return f"{index + 1:>3}  {block.kind}  {text}"

    def refresh_list(self) -> None:
        self.block_list.delete(0, "end")
        page = self.page()
        if page is None:
            return
        for i, block in enumerate(page.blocks):
            self.block_list.insert("end", self.list_item(i, block))
            self.color_list_item(i, block)

    def color_list_item(self, index: int, block: gt.Block) -> None:
        color = "#999" if block.excluded else "#c00000" if block_problems(block) else "black"
        self.block_list.itemconfigure(index, foreground=color)

    def refresh_list_item(self, index: int) -> None:
        block = self.page().blocks[index]
        self.block_list.delete(index)
        self.block_list.insert(index, self.list_item(index, block))
        self.color_list_item(index, block)
        if index == self.selected:
            self.block_list.selection_set(index)

    def select(self, index: int | None) -> None:
        self.selected = index
        self.block_list.selection_clear(0, "end")
        if index is not None:
            self.block_list.selection_set(index)
            self.block_list.see(index)
        self.load_editor()
        self.draw_boxes()

    def load_editor(self) -> None:
        block = self.current_block()
        self.loading = True
        self.ruby_error = ""
        state = ["!disabled"] if block else ["disabled"]
        for widget in self.editor_widgets:
            widget.state(state)
        self.text.configure(state="normal")
        self.text.delete("1.0", "end")
        if block:
            self.editor.configure(text=f"選取的區塊：{self.selected + 1}")
            self.kind_var.set(block.kind)
            self.direction_var.set(block.direction)
            self.size_var.set(block.size)
            self.text.insert("1.0", "\n".join(block.lines))
            self.ruby_var.set(format_ruby(block.ruby))
            self.reference_var.set(block.reference)
            self.language_var.set(block.language)
            self.note_var.set(block.note)
            self.excluded_var.set(block.excluded)
            self.box_var.set(",".join(map(str, block.box)))
        else:
            self.editor.configure(text="選取的區塊（在圖上點一個框，或拖曳新增）")
            for variable in (self.kind_var, self.direction_var, self.size_var, self.ruby_var,
                             self.reference_var, self.language_var, self.note_var, self.box_var):
                variable.set("")
            self.excluded_var.set(False)
            self.text.configure(state="disabled")
        self.text.edit_reset()
        self.text.edit_modified(False)
        self.loading = False
        self.show_problems()

    def show_problems(self) -> None:
        block = self.current_block()
        problems = block_problems(block) if block else []
        if self.ruby_error:
            problems.append(f"ルビ：{self.ruby_error}")
        self.problem_label.configure(text="\n".join(problems))

    def on_text_modified(self, _event) -> None:
        if self.text.edit_modified():
            self.text.edit_modified(False)
            self.on_field_changed("text")

    def on_field_changed(self, name: str) -> None:
        block = self.current_block()
        if self.loading or block is None:
            return
        if name == "kind":
            block.kind = self.kind_var.get()
            self.last_kind = block.kind
        elif name == "direction":
            block.direction = self.direction_var.get()
        elif name == "size":
            block.size = self.size_var.get()
        elif name == "text":
            block.lines = [line.strip() for line in self.text.get("1.0", "end").splitlines()
                           if line.strip()]
        elif name == "ruby":
            try:
                block.ruby = parse_ruby(self.ruby_var.get())
                self.ruby_error = ""
            except ValueError as error:
                self.ruby_error = str(error)
        elif name == "reference":
            block.reference = self.reference_var.get().strip()
        elif name == "language":
            block.language = self.language_var.get().strip()
        elif name == "note":
            block.note = self.note_var.get().strip()
            self.loading = True
            self.excluded_var.set(block.excluded)
            self.loading = False
        elif name == "excluded":
            note = block.note
            if self.excluded_var.get() and not block.excluded:
                note = EXCLUDE_PREFIX + note
            elif not self.excluded_var.get() and block.excluded:
                note = note[len("不評測"):].lstrip("：:").strip()
            block.note = note
            self.loading = True
            self.note_var.set(note)
            self.loading = False
        self.set_dirty()
        self.refresh_list_item(self.selected)
        self.show_problems()
        if name in ("kind", "note", "excluded"):
            self.draw_boxes()

    def apply_box_field(self) -> None:
        block = self.current_block()
        if block is None or self.loading or self.image is None:
            return
        try:
            values = [int(v) for v in self.box_var.get().replace(" ", "").split(",")]
            if len(values) != 4:
                raise ValueError
        except ValueError:
            self.box_var.set(",".join(map(str, block.box)))
            return
        box = normalize_box(*values, *self.image.size)
        if box != block.box and box[2] > box[0] and box[3] > box[1]:
            self.push_undo()
            block.box = box
            self.set_dirty()
            self.draw_boxes()
        self.box_var.set(",".join(map(str, block.box)))

    def on_list_select(self, _event) -> None:
        selection = self.block_list.curselection()
        if selection and selection[0] != self.selected:
            self.select(selection[0])
            self.ensure_visible(self.current_block().box)

    # ---- 區塊的新增、刪除、排序、復原

    def push_undo(self) -> None:
        self.undo.append((self.page_index, copy.deepcopy(self.page().blocks)))
        del self.undo[:-100]

    def on_undo_key(self, event) -> str | None:
        if event.widget is self.text or isinstance(event.widget, (tk.Entry, ttk.Entry)):
            return None  # 文字欄位有自己的復原
        self.undo_last()
        return "break"

    def undo_last(self) -> None:
        if not self.undo:
            self.status.set("沒有可以復原的變更")
            return
        index, blocks = self.undo.pop()
        self.doc.pages[index].blocks = blocks
        self.set_dirty()
        if index != self.page_index:
            self.go_page(index)
        else:
            self.refresh_pages()
            self.refresh_list()
            self.select(None)
        self.status.set("已復原")

    def delete_block(self) -> None:
        if self.current_block() is None:
            return
        self.push_undo()
        del self.page().blocks[self.selected]
        self.set_dirty()
        self.refresh_pages()
        self.refresh_list()
        self.select(None)

    def move_block(self, step: int) -> None:
        page, index = self.page(), self.selected
        if page is None or index is None or not 0 <= index + step < len(page.blocks):
            return
        self.push_undo()
        blocks = page.blocks
        blocks[index], blocks[index + step] = blocks[index + step], blocks[index]
        self.set_dirty()
        self.refresh_list()
        self.select(index + step)

    def import_draft(self) -> None:
        page = self.page()
        if page is None:
            return
        blocks = draft_blocks(self.doc.category, page.image)
        if not blocks:
            messagebox.showinfo("匯入草稿", "這張截圖沒有草稿（產生草稿的方法見 tools/eval/README.md）")
            return
        if page.blocks and not messagebox.askyesno(
                "匯入草稿", f"這張已經有 {len(page.blocks)} 個區塊，"
                            f"草稿的 {len(blocks)} 個區塊會加在後面，要繼續嗎？"):
            return
        self.push_undo()
        page.blocks.extend(blocks)
        self.set_dirty()
        self.refresh_pages()
        self.refresh_list()
        self.select(None)
        self.status.set(f"匯入了 {len(blocks)} 個草稿區塊，備註裡有草稿的編號和模型的分數")

    def recognize(self) -> None:
        block = self.current_block()
        if block is None or self.image is None:
            self.status.set("先選取一個框")
            return
        if self.ocr_busy:
            return
        if block.lines and not messagebox.askyesno("辨識文字", "用辨識結果取代目前的文字嗎？"):
            return
        language = block.language or self.doc.category.split("-")[0]
        page_index, box, direction = self.page_index, block.box, block.direction
        image = self.image.copy()  # 在另一個執行緒辨識，不和畫面共用
        self.ocr_busy = True
        self.status.set("辨識中…")

        # 在另一個執行緒辨識；Tk 只能在主執行緒呼叫，結果放進佇列，由主執行緒定時來取
        results: queue.Queue = queue.Queue()

        def work() -> None:
            try:
                results.put((recognize_box(image, box, language, direction), ""))
            except Exception as exception:  # 顯示給使用者看
                results.put(([], str(exception)))

        def poll() -> None:
            try:
                lines, error = results.get_nowait()
            except queue.Empty:
                self.root.after(100, poll)
                return
            self.on_recognized(page_index, block, lines, error)

        threading.Thread(target=work, daemon=True).start()
        self.root.after(100, poll)

    def on_recognized(self, page_index: int, block: gt.Block, lines: list[str],
                      error: str) -> None:
        self.ocr_busy = False
        if error:
            self.status.set("辨識失敗")
            messagebox.showerror("辨識失敗", error)
            return
        page = self.doc.pages[page_index] if page_index < len(self.doc.pages) else None
        if page is None or not any(b is block for b in page.blocks):
            return  # 辨識的期間區塊被刪掉了
        if not lines:
            self.status.set("框裡沒有辨識到文字")
            return
        self.undo.append((page_index, copy.deepcopy(page.blocks)))
        block.lines = lines
        self.set_dirty()
        if page_index == self.page_index:
            self.refresh_list()
            self.select(next(i for i, b in enumerate(page.blocks) if b is block))
        self.status.set(f"辨識完成：{len(lines)} 行（請核對）")

    # ---- 畫面

    def to_canvas(self, x: float, y: float) -> tuple[float, float]:
        return (x - self.origin[0]) * self.scale, (y - self.origin[1]) * self.scale

    def to_image(self, cx: float, cy: float) -> tuple[float, float]:
        return self.origin[0] + cx / self.scale, self.origin[1] + cy / self.scale

    def fit(self) -> None:
        if self.image is None:
            self.render()
            return
        width = max(1, self.canvas.winfo_width())
        height = max(1, self.canvas.winfo_height())
        image_width, image_height = self.image.size
        self.auto_fit = True
        self.scale = min(width / image_width, height / image_height) * 0.98
        self.origin = ((image_width - width / self.scale) / 2,
                       (image_height - height / self.scale) / 2)
        self.render()

    def ensure_visible(self, box: Box) -> None:
        width, height = self.canvas.winfo_width(), self.canvas.winfo_height()
        x0, y0 = self.to_canvas(box[0], box[1])
        x1, y1 = self.to_canvas(box[2], box[3])
        if x0 >= 0 and y0 >= 0 and x1 <= width and y1 <= height:
            return
        center_x, center_y = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
        self.origin = (center_x - width / 2 / self.scale, center_y - height / 2 / self.scale)
        self.auto_fit = False
        self.render()

    def schedule_render(self) -> None:
        if not self.render_pending:
            self.render_pending = True
            self.root.after_idle(self.render)

    def render(self) -> None:
        """只把看得到的部分縮放後畫出來（4K 的截圖放大時也不會用掉太多記憶體）。"""
        self.render_pending = False
        self.canvas.delete("all")
        self.photo = None
        if self.image is not None:
            width, height = self.canvas.winfo_width(), self.canvas.winfo_height()
            image_width, image_height = self.image.size
            left, top = self.to_image(0, 0)
            right, bottom = self.to_image(width, height)
            left, top = max(0, math.floor(left)), max(0, math.floor(top))
            right, bottom = min(image_width, math.ceil(right)), min(image_height, math.ceil(bottom))
            if right > left and bottom > top:
                size = (max(1, round((right - left) * self.scale)),
                        max(1, round((bottom - top) * self.scale)))
                resample = (self.pil_image.Resampling.NEAREST if self.scale >= 3
                            else self.pil_image.Resampling.BILINEAR)
                region = self.image.crop((left, top, right, bottom)).resize(
                    size, resample, reducing_gap=2.0 if self.scale < 1 else None)
                self.photo = self.pil_tk.PhotoImage(region)
                self.canvas.create_image(*self.to_canvas(left, top), image=self.photo,
                                         anchor="nw")
        self.zoom_label.configure(text=f"{self.scale * 100:.0f}%")
        self.draw_boxes()

    def draw_boxes(self) -> None:
        self.canvas.delete("box")
        page = self.page()
        if page is None or self.image is None:
            return
        for i, block in enumerate(page.blocks):
            if i != self.selected:
                self.draw_block(i, block, False)
        if self.selected is not None:
            self.draw_block(self.selected, page.blocks[self.selected], True)

    def draw_block(self, index: int, block: gt.Block, selected: bool) -> None:
        color = SELECTED if selected else EXCLUDED if block.excluded else EVALUATED
        x0, y0 = self.to_canvas(block.box[0], block.box[1])
        x1, y1 = self.to_canvas(block.box[2], block.box[3])
        canvas = self.canvas
        canvas.create_rectangle(x0, y0, x1, y1, outline=color, width=3 if selected else 2,
                                tags="box")
        inside = y0 < 18  # 框貼著畫面上緣時，標籤畫在框裡面
        label = canvas.create_text(x0 + 4, y0 + 1 if inside else y0 - 1,
                                   anchor="nw" if inside else "sw", text=f"{index + 1} {block.kind}",
                                   fill="white", font=LABEL_FONT, tags="box")
        lx0, ly0, lx1, ly1 = canvas.bbox(label)
        background = canvas.create_rectangle(lx0 - 3, ly0, lx1 + 3, ly1, fill=color, outline=color,
                                             tags="box")
        canvas.tag_lower(background, label)
        if selected:
            r = 4
            for hx in (x0, (x0 + x1) / 2, x1):
                for hy in (y0, (y0 + y1) / 2, y1):
                    if (hx, hy) != ((x0 + x1) / 2, (y0 + y1) / 2):
                        canvas.create_rectangle(hx - r, hy - r, hx + r, hy + r, fill="white",
                                                outline=color, tags="box")

    # ---- 滑鼠

    def on_press(self, event) -> None:
        if self.root.focus_get() is self.box_entry:
            self.apply_box_field()
        self.canvas.focus_set()
        page = self.page()
        if page is None or self.image is None:
            return
        x, y = self.to_image(event.x, event.y)
        shift = bool(event.state & 0x0001)
        block = self.current_block()
        if block is not None and not shift:
            handle = hit_handle(block.box, x, y, HANDLE / self.scale)
            if handle:
                self.drag = {"mode": "resize", "handle": handle, "start": (x, y),
                             "box": block.box, "undo": copy.deepcopy(page.blocks)}
                return
        index = None if shift else block_at(page.blocks, x, y)
        if index is not None:
            if index != self.selected:
                self.select(index)
            self.drag = {"mode": "move", "start": (x, y), "box": page.blocks[index].box,
                         "undo": copy.deepcopy(page.blocks)}
            return
        if self.selected is not None:
            self.select(None)
        self.drag = {"mode": "create", "start": (x, y)}

    def on_drag(self, event) -> None:
        if self.drag is None or self.image is None:
            return
        x, y = self.to_image(event.x, event.y)
        start_x, start_y = self.drag["start"]
        size = self.image.size
        if self.drag["mode"] == "create":
            box = normalize_box(start_x, start_y, x, y, *size)
            self.drag["new"] = box
            self.canvas.delete("rubber")
            self.canvas.create_rectangle(*self.to_canvas(box[0], box[1]),
                                         *self.to_canvas(box[2], box[3]), outline=SELECTED,
                                         dash=(4, 2), width=2, tags="rubber")
            return
        block = self.current_block()
        if self.drag["mode"] == "move":
            block.box = move_box(self.drag["box"], x - start_x, y - start_y, *size)
        else:
            block.box = resize_box(self.drag["box"], self.drag["handle"], x - start_x, y - start_y,
                                   *size)
        self.loading = True
        self.box_var.set(",".join(map(str, block.box)))
        self.loading = False
        self.draw_boxes()

    def on_release(self, _event) -> None:
        drag, self.drag = self.drag, None
        if drag is None:
            return
        page = self.page()
        if drag["mode"] == "create":
            self.canvas.delete("rubber")
            box = drag.get("new")
            if box is None or min(box[2] - box[0], box[3] - box[1]) * self.scale < 4:
                return  # 只是點一下
            self.push_undo()
            page.blocks.append(gt.Block(self.last_kind, guess_direction(box), "normal", box))
            self.set_dirty()
            self.refresh_pages()
            self.refresh_list()
            self.select(len(page.blocks) - 1)
            self.text.focus_set()
            return
        block = self.current_block()
        if block.box[2] <= block.box[0] or block.box[3] <= block.box[1]:
            block.box = drag["box"]  # 拉成空的框：還原
            self.draw_boxes()
        if block.box != drag["box"]:
            self.undo.append((self.page_index, drag["undo"]))
            self.set_dirty()
        self.load_editor()

    def on_motion(self, event) -> None:
        page = self.page()
        if page is None or self.image is None:
            return
        x, y = self.to_image(event.x, event.y)
        self.position.set(f"({x:.0f}, {y:.0f})")
        cursor = "crosshair"
        block = self.current_block()
        if block is not None:
            cursor = CURSORS.get(hit_handle(block.box, x, y, HANDLE / self.scale), cursor)
        if cursor == "crosshair" and block_at(page.blocks, x, y) is not None:
            cursor = "fleur"
        if cursor != self.cursor:
            self.cursor = cursor
            self.canvas.configure(cursor=cursor)

    def on_wheel(self, event) -> None:
        x, y = self.to_image(event.x, event.y)
        factor = ZOOM_STEP if event.delta > 0 else 1 / ZOOM_STEP
        self.auto_fit = False
        self.scale = min(MAX_ZOOM, max(MIN_ZOOM, self.scale * factor))
        self.origin = (x - event.x / self.scale, y - event.y / self.scale)
        self.schedule_render()

    def on_pan_start(self, event) -> None:
        self.auto_fit = False
        self.pan = (event.x, event.y, self.origin)

    def on_pan(self, event) -> None:
        if self.pan is None:
            return
        start_x, start_y, (origin_x, origin_y) = self.pan
        self.origin = (origin_x - (event.x - start_x) / self.scale,
                       origin_y - (event.y - start_y) / self.scale)
        self.schedule_render()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("category", nargs="?", help="例如 ja-manga（不填就開第一個分類）")
    args = parser.parse_args()
    try:
        import ctypes
        ctypes.windll.shcore.SetProcessDpiAwareness(1)  # 高 DPI 螢幕上字才不會模糊
    except (AttributeError, OSError):
        pass
    root = tk.Tk()
    App(root, args.category)
    root.mainloop()


if __name__ == "__main__":
    main()
