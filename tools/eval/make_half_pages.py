"""把每一頁切成上下兩半，連正確答案一起裁切：模擬「透鏡只蓋住半頁」。

主程式從來不會 OCR 整頁，餵給 OCR 和 comic-text-detector 的是透鏡裁下來的一塊。
整頁上量出來的分段效果，不一定在裁切過的畫面上也成立（comic-text-detector 是在整頁上訓練的，
裁切後的畫面放大到 1024 再送進去），所以要在裁切的畫面上再量一次。

正確答案只保留完整落在那一半裡的區塊，座標換成裁切後的；被切線切到的區塊不評分
（主程式本來就會把碰到透鏡邊緣的殘缺句子丟掉，見 design.md 4.4）。

    tools/eval/.venv-manga/Scripts/python tools/eval/make_half_pages.py \\
        --images testdata/private/ja-manga --ground-truth testdata/private/ja-manga/ground_truth.txt \\
        --output build/ocr_eval/m2-02/halves

輸出的圖片和 ground_truth.txt 都只放在本機（含有截圖內容）。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from PIL import Image

BLOCK = re.compile(r"^\[(\S+)\s+(\S+)\s+(\S+)\s+(\d+),(\d+),(\d+),(\d+)\](.*)$")


def read_pages(path: Path) -> dict[str, list[list[str]]]:
    """檔名 → 區塊清單（每個區塊是它原本的幾行文字，第一行是 [種類 方向 大小 座標]）。"""
    pages: dict[str, list[list[str]]] = {}
    current = None
    block: list[str] | None = None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("== "):
            current = line[3:].strip()
            pages[current] = []
            block = None
        elif current and BLOCK.match(line):
            block = [line]
            pages[current].append(block)
        elif block is not None and line.strip():
            block.append(line)
        else:
            block = None
    return pages


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--images", required=True)
    parser.add_argument("--ground-truth", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    pages = read_pages(Path(args.ground_truth))
    lines = ["# make_half_pages.py 從整頁的正確答案裁出來的，只保留完整落在那一半裡的區塊"]
    kept = dropped = 0
    for name, blocks in pages.items():
        with Image.open(Path(args.images) / name) as page:
            width, height = page.size
            middle = height // 2
            for label, top, bottom in (("上", 0, middle), ("下", middle, height)):
                half_name = f"{Path(name).stem}-{label}.png"
                page.crop((0, top, width, bottom)).save(output / half_name)
                lines.append("")
                lines.append(f"== {half_name}")
                for block in blocks:
                    found = BLOCK.match(block[0])
                    l, t, r, b = (int(found.group(i)) for i in range(4, 8))
                    if t >= top and b <= bottom:
                        kept += 1
                        header = (f"[{found.group(1)} {found.group(2)} {found.group(3)} "
                                  f"{l},{t - top},{r},{b - top}]{found.group(8)}")
                        lines.append("")
                        lines.append(header)
                        lines.extend(block[1:])
                    elif label == "上" and t < middle < b:
                        dropped += 1  # 跨過切線的區塊只算一次
    (output / "ground_truth.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(pages)} 頁 → {2 * len(pages)} 張半頁；保留 {kept} 個區塊，被切線切到而不評分的 {dropped} 個")
    return 0


if __name__ == "__main__":
    sys.exit(main())
