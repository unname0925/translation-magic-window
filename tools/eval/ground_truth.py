"""正確答案（ground truth）的文字格式：讀取、寫出、正規化。只用 Python 標準函式庫。

每個分類一個檔案 testdata/private/<分類>/ground_truth.txt（有版權，不進版本控制）。
格式設計成可以直接在文字編輯器裡校對：

    == page01.png
    配對: en-manga/page01.png

    [對白 直 中 356,70,483,285]
    えっと
    明日は皆で
    東京に行くんだ
    ルビ: 明日=あした 皆=みんな 東京=とうきょう 行=い
    參考: UM... TOMORROW WE'RE ALL GOING TO TOKYO.

- `== 檔名` 開始一張截圖；`配對:` 是另一種語言的同一頁（選填）。
- `[種類 方向 字級 x0,y0,x1,y1]` 開始一個區塊，接著每一行是區塊中的一行文字（閱讀順序）。
  - 種類：對白、旁白、擬聲詞、註、標題、介面、其他
  - 方向：直、橫；字級：小、中、大
- `ルビ:` 以空白分隔的「本文=讀音」，依出現順序對應；另有含義的在最後加 `!`（例如 本気=マジ!）。
- `參考:` 另一種語言版本的對應譯文；`語言:` 這個區塊的語言和整張截圖不同時填寫（例如 ja）；
  `備註:` 給校對者看的說明；以「不評測」開頭的區塊（例如散落一整片、無法逐字標註的擬聲詞）
  評測時略過。
- 區塊外 `#` 開頭的行是註解；區塊內的 `#` 開頭的行是文字（網頁上「#今日のおすすめ」這類標題）。
"""

from __future__ import annotations

import re
import unicodedata
from dataclasses import dataclass, field
from pathlib import Path

KINDS = ("對白", "旁白", "擬聲詞", "註", "標題", "介面", "其他")
DIRECTIONS = {"直": "vertical", "橫": "horizontal"}
SIZES = {"小": "small", "中": "normal", "大": "large"}
_HEADER = re.compile(r"^\[(\S+) (\S+) (\S+) (-?\d+),(-?\d+),(-?\d+),(-?\d+)\]$")
_FIELDS = ("ルビ:", "參考:", "語言:", "備註:")
_QUOTES = str.maketrans({"“": '"', "”": '"', "„": '"', "‟": '"', "‘": "'", "’": "'", "‚": "'",
                         "‛": "'", "〜": "~"})


class FormatError(ValueError):
    pass


@dataclass
class Ruby:
    base: str
    reading: str
    meaning: bool = False  # True：另有含義（要和本文一起翻譯）


@dataclass
class Block:
    kind: str
    direction: str  # vertical / horizontal
    size: str  # small / normal / large
    box: tuple[int, int, int, int]
    lines: list[str] = field(default_factory=list)
    ruby: list[Ruby] = field(default_factory=list)
    reference: str = ""
    language: str = ""
    note: str = ""

    @property
    def excluded(self) -> bool:
        """評測時略過（備註以「不評測」開頭）。"""
        return self.note.startswith("不評測")

    def text(self, language: str) -> str:
        """整個區塊的文字：日文直接相接；英文、韓文的詞之間有空白，用空白相接。"""
        separator = " " if language in ("en", "ko") else ""
        return separator.join(self.lines)


@dataclass
class Page:
    image: str
    pair: str = ""
    blocks: list[Block] = field(default_factory=list)


def is_markup(line: str) -> bool:
    """這一行在區塊裡會被當成格式（新的截圖、區塊或欄位），不能當作文字。"""
    line = line.strip()
    return line.startswith(("== ",) + _FIELDS) or bool(_HEADER.match(line))


def parse(text: str) -> list[Page]:
    pages: list[Page] = []
    block: Block | None = None
    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.rstrip()
        where = f"line {number}"
        if not line.strip():
            block = None
            continue
        if block is None and line.lstrip().startswith("#"):
            continue
        if line.startswith("== "):
            pages.append(Page(image=line[3:].strip()))
            block = None
            continue
        if not pages:
            raise FormatError(f"{where}: text before the first '== image' line")
        page = pages[-1]
        if line.startswith("配對:") and block is None:
            page.pair = line[3:].strip()
            continue
        header = _HEADER.match(line)
        if header:
            kind, direction, size = header.group(1), header.group(2), header.group(3)
            if kind not in KINDS:
                raise FormatError(f"{where}: unknown kind {kind!r}")
            if direction not in DIRECTIONS:
                raise FormatError(f"{where}: unknown direction {direction!r}")
            if size not in SIZES:
                raise FormatError(f"{where}: unknown size {size!r}")
            x0, y0, x1, y1 = (int(header.group(i)) for i in range(4, 8))
            if x1 <= x0 or y1 <= y0:
                raise FormatError(f"{where}: empty box")
            block = Block(kind, DIRECTIONS[direction], SIZES[size], (x0, y0, x1, y1))
            page.blocks.append(block)
            continue
        if block is None:
            raise FormatError(f"{where}: text outside a block: {line!r}")
        if line.startswith("ルビ:"):
            for item in line[3:].split():
                if "=" not in item:
                    raise FormatError(f"{where}: ruby must be base=reading: {item!r}")
                base, reading = item.split("=", 1)
                meaning = reading.endswith("!")
                block.ruby.append(Ruby(base, reading.rstrip("!"), meaning))
        elif line.startswith("參考:"):
            block.reference = line[3:].strip()
        elif line.startswith("語言:"):
            block.language = line[3:].strip()
        elif line.startswith("備註:"):
            block.note = line[3:].strip()
        else:
            if block.ruby or block.reference or block.language or block.note:
                raise FormatError(f"{where}: text line after ルビ/參考/語言/備註")
            block.lines.append(line.strip())
    for page in pages:
        for i, block in enumerate(page.blocks, start=1):
            if not block.lines:
                raise FormatError(f"{page.image} block {i}: no text")
            joined = "".join(block.lines)
            position = 0
            for ruby in block.ruby:
                found = joined.find(ruby.base, position)
                if found < 0:
                    raise FormatError(f"{page.image} block {i}: ruby base {ruby.base!r} not found "
                                      f"in order")
                position = found + len(ruby.base)
    return pages


def load(path: Path) -> list[Page]:
    return parse(path.read_text(encoding="utf-8-sig"))


def dump(pages: list[Page]) -> str:
    kind_names = {v: k for k, v in DIRECTIONS.items()}
    size_names = {v: k for k, v in SIZES.items()}
    out: list[str] = []
    for page in pages:
        out.append(f"== {page.image}")
        if page.pair:
            out.append(f"配對: {page.pair}")
        for block in page.blocks:
            out.append("")
            x0, y0, x1, y1 = block.box
            out.append(f"[{block.kind} {kind_names[block.direction]} {size_names[block.size]} "
                       f"{x0},{y0},{x1},{y1}]")
            out.extend(block.lines)
            if block.ruby:
                out.append("ルビ: " + " ".join(
                    f"{r.base}={r.reading}{'!' if r.meaning else ''}" for r in block.ruby))
            if block.language:
                out.append(f"語言: {block.language}")
            if block.reference:
                out.append(f"參考: {block.reference}")
            if block.note:
                out.append(f"備註: {block.note}")
        out.append("")
    return "\n".join(out) + "\n"


def normalize(text: str) -> str:
    """比較文字時的正規化：全形半形統一（NFKC）、刪節號、引號、波浪號統一、去掉所有空白。

    manga-ocr 會把「…」輸出成「．．．」，把英數字轉成全形；英文漫畫常用彎引號（“ ” ’），
    OCR 多半輸出直引號；日文網頁的「〜」「～」和「~」看起來幾乎一樣。這些差異不算辨識錯誤。
    """
    text = unicodedata.normalize("NFKC", text)
    text = text.translate(_QUOTES)
    text = text.replace("…", "...").replace("‥", "..")
    text = re.sub(r"[・.]{2,}", lambda m: "." * (m.end() - m.start()), text)
    return "".join(text.split())
