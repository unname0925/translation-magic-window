"""M0-10：檢查正確答案並畫出校對用的圖。

    tools/eval/.venv/Scripts/python tools/eval/review_ground_truth.py [--categories ja-manga ...]

對 testdata/private/<分類>/ground_truth.txt：
- 讀取並檢查格式（ground_truth.load），確認每張截圖都有標註、區塊沒有超出截圖。
- 在截圖上畫出每個區塊（評測的紅色、不評測的灰色），左上角標「編號 種類」，
  輸出到 build/gt_review/<分類>/<截圖檔名>.png（有版權，不進版本控制）。
  校對時一邊看圖，一邊改 ground_truth.txt，區塊依檔案裡的順序從 1 編號。
- 印出各分類的截圖、區塊數量。
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

import ground_truth as gt

REPO_ROOT = Path(__file__).resolve().parents[2]
PRIVATE = REPO_ROOT / "testdata" / "private"
OUTPUT = REPO_ROOT / "build" / "gt_review"
LABEL_FONT = "C:/Windows/Fonts/msjhbd.ttc"  # 微軟正黑體，標籤有中文
EVALUATED = (230, 0, 0)
EXCLUDED = (128, 128, 128)


def check(category: str, pages: list[gt.Page]) -> list[str]:
    problems = []
    images = sorted(p.name for p in (PRIVATE / category).iterdir()
                    if p.suffix.lower() in (".png", ".jpg", ".webp"))
    names = [page.image for page in pages]
    for name in sorted(set(images) - set(names)):
        problems.append(f"{name}: 沒有標註")
    for name in sorted(set(names) - set(images)):
        problems.append(f"{name}: 找不到截圖")
    for name in sorted({n for n in names if names.count(n) > 1}):
        problems.append(f"{name}: 標註了不只一次")
    for page in pages:
        path = PRIVATE / category / page.image
        if not path.exists():
            continue
        width, height = Image.open(path).size
        for i, block in enumerate(page.blocks, start=1):
            x0, y0, x1, y1 = block.box
            if x0 < 0 or y0 < 0 or x1 > width or y1 > height:
                problems.append(f"{page.image} 區塊 {i}: {block.box} 超出截圖 {width}x{height}")
    return problems


def draw_page(category: str, page: gt.Page) -> None:
    image = Image.open(PRIVATE / category / page.image).convert("RGB")
    draw = ImageDraw.Draw(image)
    scale = max(1, image.width // 1200)
    font = ImageFont.truetype(LABEL_FONT, 16 * scale)
    for i, block in enumerate(page.blocks, start=1):
        color = EXCLUDED if block.excluded else EVALUATED
        x0, y0, x1, y1 = block.box
        draw.rectangle((x0, y0, x1, y1), outline=color, width=2 * scale)
        text = f"{i} {block.kind}"
        tw = draw.textlength(text, font=font)
        top = max(0, y0 - 20 * scale)
        draw.rectangle((x0, top, x0 + tw + 6 * scale, top + 20 * scale), fill=color)
        draw.text((x0 + 3 * scale, top), text, font=font, fill="white")
    output = OUTPUT / category / f"{Path(page.image).stem}.png"
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--categories", nargs="+")
    args = parser.parse_args()

    categories = args.categories or sorted(
        p.name for p in PRIVATE.iterdir() if (p / "ground_truth.txt").exists())
    failed = False
    for category in categories:
        pages = gt.load(PRIVATE / category / "ground_truth.txt")
        problems = check(category, pages)
        for page in pages:
            if (PRIVATE / category / page.image).exists():
                draw_page(category, page)
        blocks = [b for page in pages for b in page.blocks]
        evaluated = sum(not b.excluded for b in blocks)
        print(f"{category}: {len(pages)} 張，{len(blocks)} 個區塊（評測 {evaluated}）")
        for problem in problems:
            print(f"  問題：{problem}")
        failed |= bool(problems)
    print(f"校對用的圖：{OUTPUT}")
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
