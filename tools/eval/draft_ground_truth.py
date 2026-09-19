"""M0-10：從模型的辨識結果產生正確答案的草稿，之後再人工校對。

輸入：tmw_ocr_cli 對 testdata/private 各分類跑出的逐行結果（build/gt_drafts/<分類>.ppocr.json）。
輸出（都在 build/gt_drafts，不進版本控制；截圖和文字有版權）：
- <分類>/ground_truth.draft.txt：標註草稿（格式見 tools/eval/ground_truth.py），
  模型的其他參考（manga-ocr 的結果、PP-OCR 的最低分數）放在「備註」
- <分類>/<截圖檔名>.vis.png：在截圖上畫出區塊編號（紅）和ルビ（藍），方便逐張檢查

草稿的做法：
1. 每一行判斷直排或橫排，字的粗細（直排是欄寬，橫排是行高）。
2. ルビ：比旁邊的本文細很多（不到 0.65 倍），而且緊貼在本文右側（直排）或上方（橫排）的行。
   依位置對應到本文的字元範圍。
3. 區塊：把方向相同、距離很近的本文行合併成一個區塊（一個對話框）。
4. 漫畫另外用 manga-ocr（逐字解碼）辨識每個區塊的裁切圖，當作第二份參考。
5. 字級：和整頁的中位數比較，分成小、一般、大。

必須用 tools/eval/.venv-manga 的 Python 執行：
    tools/eval/.venv-manga/Scripts/python tools/eval/draft_ground_truth.py [--categories ja-manga ...]
"""

from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

import ground_truth as gt
from ocr_lines import Line, find_ruby, overlap, to_lines

REPO_ROOT = Path(__file__).resolve().parents[2]
PRIVATE = REPO_ROOT / "testdata" / "private"
DRAFTS = REPO_ROOT / "build" / "gt_drafts"
LABEL_FONT = "C:/Windows/Fonts/arialbd.ttf"

def ruby_span(base: Line, ruby: Line) -> tuple[int, int]:
    """ルビ對應到的本文字元範圍（假設本文的每個字等寬）。"""
    n = max(1, len(base.text))
    step = base.length / n
    start_pos = (ruby.y0 - base.y0) if base.vertical else (ruby.x0 - base.x0)
    end_pos = (ruby.y1 - base.y0) if base.vertical else (ruby.x1 - base.x0)
    chars = [i for i in range(n)
             if overlap(int(i * step), int((i + 1) * step), int(start_pos), int(end_pos)) >= step * 0.5]
    return (chars[0], chars[-1] + 1) if chars else (0, 0)


def group_blocks(lines: list[Line]) -> list[list[Line]]:
    """方向相同、距離很近的本文行合併成區塊。"""
    mains = [line for line in lines if line.ruby_of is None]
    parent = {line.index: line.index for line in mains}

    def find(i: int) -> int:
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for a in mains:
        for b in mains:
            if a.index >= b.index or a.vertical != b.vertical:
                continue
            thickness = max(a.thickness, b.thickness)
            if min(a.thickness, b.thickness) < thickness * 0.6:
                continue  # 字的大小差太多，不是同一個對話框
            # 考慮中間夾著ルビ：允許的間距是一個字寬
            if a.vertical:
                gap = max(a.x0, b.x0) - min(a.x1, b.x1)
                aligned = overlap(a.y0, a.y1, b.y0, b.y1) > 0 and abs(a.y0 - b.y0) < thickness * 2
            else:
                gap = max(a.y0, b.y0) - min(a.y1, b.y1)
                aligned = overlap(a.x0, a.x1, b.x0, b.x1) > 0
            if aligned and gap <= thickness * 1.0:
                parent[find(a.index)] = find(b.index)

    groups: dict[int, list[Line]] = {}
    for line in mains:
        groups.setdefault(find(line.index), []).append(line)
    blocks = list(groups.values())
    for block in blocks:
        # 閱讀順序：直排由右到左，橫排由上到下
        if block[0].vertical:
            block.sort(key=lambda l: -l.x1)
        else:
            block.sort(key=lambda l: l.y0)
    # 區塊之間：由上到下、同一列由右到左（漫畫）
    blocks.sort(key=lambda b: (min(l.y0 for l in b) // 80, -max(l.x1 for l in b)))
    return blocks


def size_level(thickness: float, median: float) -> str:
    if thickness < median * 0.7:
        return "small"
    if thickness > median * 1.5:
        return "large"
    return "normal"


def draft_image(image_path: Path, ocr: dict, language: str, content: str, manga_ocr) -> dict:
    lines = to_lines(ocr["lines"])
    if language == "ja":  # 只有日文有振り仮名
        find_ruby(lines)
    blocks = group_blocks(lines)
    by_index = {line.index: line for line in lines}
    mains = [line for line in lines if line.ruby_of is None]
    median = statistics.median([line.thickness for line in mains]) if mains else 1
    image = Image.open(image_path).convert("RGB")

    result_blocks = []
    for number, block in enumerate(blocks, start=1):
        x0 = min(l.x0 for l in block)
        y0 = min(l.y0 for l in block)
        x1 = max(max(l.x1 for l in block), *[by_index[r].x1 for l in block for r in l.rubies] or [0])
        y1 = max(l.y1 for l in block)
        if not block[0].vertical:
            y0 = min(y0, *[by_index[r].y0 for l in block for r in l.rubies] or [y0])
        ruby = []
        for line_no, line in enumerate(block):
            for r in line.rubies:
                start, end = ruby_span(line, by_index[r])
                reading = "".join(by_index[r].text.split()).replace("=", "")
                ruby.append({"line": line_no, "start": start, "end": end,
                             "base": line.text[start:end], "reading": reading,
                             "meaning": False})
        entry = {
            "id": number,
            "kind": "dialogue",
            "direction": "vertical" if block[0].vertical else "horizontal",
            "box": [x0, y0, x1, y1],
            "lines": [l.text for l in block],
            "ruby": ruby,
            "size": size_level(statistics.median([l.thickness for l in block]), median),
            "draft": {"ppocr_min_score": round(min(l.score for l in block), 3)},
        }
        if manga_ocr is not None:
            margin = 6
            crop = image.crop((max(0, x0 - margin), max(0, y0 - margin),
                               min(image.width, x1 + margin), min(image.height, y1 + margin)))
            entry["draft"]["manga_ocr"] = manga_ocr(crop, method="greedy")[0]
        result_blocks.append(entry)

    return {
        "schema": 1,
        "image": image_path.name,
        "language": language,
        "content": content,
        "status": "draft",
        "blocks": result_blocks,
    }


def visualize(image_path: Path, draft: dict, lines: list[Line], output: Path) -> None:
    image = Image.open(image_path).convert("RGB")
    draw = ImageDraw.Draw(image)
    scale = max(1, image.width // 1200)
    label = ImageFont.truetype(LABEL_FONT, 18 * scale)
    for line in lines:
        if line.ruby_of is not None:
            draw.rectangle((line.x0, line.y0, line.x1, line.y1), outline=(0, 90, 255), width=scale)
    for block in draft["blocks"]:
        x0, y0, x1, y1 = block["box"]
        draw.rectangle((x0, y0, x1, y1), outline=(230, 0, 0), width=2 * scale)
        text = str(block["id"])
        tw = draw.textlength(text, font=label)
        draw.rectangle((x0, y0 - 22 * scale, x0 + tw + 6 * scale, y0), fill=(230, 0, 0))
        draw.text((x0 + 3 * scale, y0 - 22 * scale), text, font=label, fill="white")
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)


def to_page(draft: dict, pair: str) -> gt.Page:
    """草稿轉成文字格式；模型的其他參考放在備註。"""
    page = gt.Page(image=draft["image"], pair=pair)
    for entry in draft["blocks"]:
        low = entry["draft"]["ppocr_min_score"]
        kind = "擬聲詞" if entry["size"] == "large" and low < 0.6 else "對白"
        notes = [f"#{entry['id']}"]
        if "manga_ocr" in entry["draft"]:
            notes.append(f"manga-ocr={entry['draft']['manga_ocr']}")
        if low < 0.8:
            notes.append(f"PP-OCR 最低分數 {low}")
        ruby = [gt.Ruby(r["base"], r["reading"]) for r in entry["ruby"] if r["base"]]
        # 依出現順序排好，讀取時才對得上；依序找不到的（例如兩段ルビ對到同一個字）捨棄
        joined = "".join(entry["lines"])
        ruby.sort(key=lambda r: joined.find(r.base))
        kept, position = [], 0
        for r in ruby:
            found = joined.find(r.base, position)
            if found >= 0:
                kept.append(r)
                position = found + len(r.base)
        ruby = kept
        page.blocks.append(gt.Block(kind, entry["direction"], entry["size"], tuple(entry["box"]),
                                    list(entry["lines"]), ruby, note="；".join(notes)))
    return page


def pairs(category: str) -> dict[str, str]:
    """日文和英文漫畫是同一部漫畫的同一頁（依檔名排序後一一對應）。"""
    language, content = category.split("-")
    other = {"ja": "en", "en": "ja"}.get(language)
    if content != "manga" or other is None or not (PRIVATE / f"{other}-{content}").exists():
        return {}
    mine = sorted(p.name for p in (PRIVATE / category).iterdir() if p.is_file())
    theirs = sorted(p.name for p in (PRIVATE / f"{other}-{content}").iterdir() if p.is_file())
    return {a: f"{other}-{content}/{b}" for a, b in zip(mine, theirs)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--categories", nargs="+")
    args = parser.parse_args()

    categories = args.categories or sorted(p.name for p in PRIVATE.iterdir() if p.is_dir())
    manga_ocr = None
    for category in categories:
        language, content = category.split("-")
        ocr_file = DRAFTS / f"{category}.ppocr.json"
        ocr = {image["image"]: image for image in
               json.loads(ocr_file.read_text(encoding="utf-8"))["images"]}
        if content == "manga" and language == "ja" and manga_ocr is None:
            import manga_onnx
            manga_ocr = manga_onnx.MangaOcrOnnx("dml")
        paired = pairs(category)
        pages = []
        for image_path in sorted((PRIVATE / category).iterdir()):
            if image_path.name not in ocr:
                continue
            draft = draft_image(image_path, ocr[image_path.name], language, content,
                                manga_ocr if (content == "manga" and language == "ja") else None)
            out_dir = DRAFTS / category
            out_dir.mkdir(parents=True, exist_ok=True)
            pages.append(to_page(draft, paired.get(image_path.name, "")))
            lines = to_lines(ocr[image_path.name]["lines"])
            if language == "ja":
                find_ruby(lines)
            visualize(image_path, draft, lines, out_dir / f"{image_path.stem}.vis.png")
            print(f"{category}/{image_path.name}: {len(draft['blocks'])} blocks, "
                  f"{sum(len(b['ruby']) for b in draft['blocks'])} ruby")
        (DRAFTS / category / "ground_truth.draft.txt").write_text(gt.dump(pages), encoding="utf-8")


if __name__ == "__main__":
    main()
