"""M2-02：C++ 版 comic-text-detector 和 Python 參考版（comic_text_detector.py）比對。

ocr_cli 加上 --comic-text 時會在每張圖的結果裡輸出 comic_blocks。這裡對同一批圖片跑 Python 版，
逐框配對（IoU 最高的那個），看兩邊找到的框數一不一樣、座標差多少。

縮放的細節兩邊不完全一樣（PIL 的雙線性縮小會先平滑，C++ 用 OpenCV 的 INTER_AREA 近似），
所以座標差個一兩個像素是預期中的（實測 79 個框裡 77 個差 1 像素以內、最大差 6）。
通過標準：框數相同、全部配對得上、95% 以上差 1 像素以內、最大不超過 8 像素。

    tools/eval/.venv-manga/Scripts/python tools/eval/check_comic_text_detector.py \\
        --cpp build/ocr_eval/m2-02/with_ctd.json --images testdata/private/ja-manga
"""

from __future__ import annotations

import argparse
import collections
import json
import sys
from pathlib import Path

from PIL import Image

from comic_text_detector import ComicTextDetector


def iou(a, b) -> float:
    width = max(0, min(a[2], b[2]) - max(a[0], b[0]))
    height = max(0, min(a[3], b[3]) - max(a[1], b[1]))
    shared = width * height
    union = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - shared
    return shared / max(union, 1e-6)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp", required=True)
    parser.add_argument("--images", required=True)
    parser.add_argument("--device", default="dml")
    args = parser.parse_args()

    detector = ComicTextDetector(args.device)
    cpp = json.loads(Path(args.cpp).read_text(encoding="utf-8"))["images"]

    total_python = total_cpp = matched = 0
    worst = 0
    differences = collections.Counter()
    only_one_side = []
    for image in cpp:
        with Image.open(Path(args.images) / image["image"]) as page:
            python_boxes = [d.box for d in detector(page)[0]]
        cpp_boxes = [tuple(b["rect"]) for b in image["comic_blocks"]]
        total_python += len(python_boxes)
        total_cpp += len(cpp_boxes)
        unused = list(cpp_boxes)
        for box in python_boxes:
            best = max(unused, key=lambda other: iou(box, other), default=None)
            if best is not None and iou(box, best) >= 0.5:
                matched += 1
                difference = max(abs(p - c) for p, c in zip(box, best))
                differences[difference] += 1
                worst = max(worst, difference)
                unused.remove(best)
            else:
                only_one_side.append((image["image"], "只有 Python", box))
        only_one_side.extend((image["image"], "只有 C++", box) for box in unused)

    print(f"Python {total_python} 個框，C++ {total_cpp} 個框，配對成功 {matched} 個")
    print(f"配對到的框，座標最大差 {worst} 像素；差幾像素 → 幾個框：{dict(sorted(differences.items()))}")
    for name, side, box in only_one_side:
        print(f"  {side}：{name} {box}")
    # 縮放的插值方式兩邊不同，偶爾有一兩個框差好幾個像素（實測 79 個框裡 77 個差 1 像素以內，
    # 最大差 6）。要抓的是移植錯誤——那會讓大部分的框都偏掉，或讓框數對不上。
    within_one = sum(count for difference, count in differences.items() if difference <= 1)
    close_enough = matched > 0 and within_one >= matched * 0.95 and worst <= 8
    return 0 if not only_one_side and close_enough else 1


if __name__ == "__main__":
    sys.exit(main())
