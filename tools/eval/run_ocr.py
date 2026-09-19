"""M0-11：用 C++ 的 tmw_ocr_cli 對真實截圖跑每一種模型組合，結果給 evaluate_ocr.py 評分。

    python tools/eval/run_ocr.py [--device dml] [--combos v6-medium ...] [--categories ja-manga ...]

輸出 build/ocr_eval/m0-11/raw/<裝置>/<組合>/<分類>.json（tmw_ocr_cli 的格式）。已經有結果的會略過
（加 --force 重跑），所以校對完正確答案後只要重新評分，不用重跑 OCR。
每張截圖跑 2 次：第一次是暖機（DirectML 第一次遇到新的輸入大小要編譯），耗時取第二次。
只用標準函式庫。
"""

from __future__ import annotations

import argparse
import subprocess
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
PRIVATE = REPO_ROOT / "testdata" / "private"
MODELS = REPO_ROOT / "models"
OCR_CLI = REPO_ROOT / "build" / "msvc-x64" / "bin" / "Release" / "tmw_ocr_cli.exe"
OUTPUT = REPO_ROOT / "build" / "ocr_eval" / "m0-11" / "raw"
IMAGE_SUFFIXES = (".png", ".jpg", ".jpeg", ".webp")

# 組合名稱 → (偵測模型, 辨識模型)。韓文辨識模型搭配各種偵測模型，也用來模擬語言判斷策略
# （同一個偵測模型的文字框完全相同，可以逐行比較兩個辨識模型）。
COMBOS = {
    "v6-medium": ("PP-OCRv6_medium_det", "PP-OCRv6_medium_rec"),
    "v6-small": ("PP-OCRv6_small_det", "PP-OCRv6_small_rec"),
    "v6-tiny": ("PP-OCRv6_tiny_det", "PP-OCRv6_tiny_rec"),
    "v5-server": ("PP-OCRv5_server_det", "PP-OCRv5_server_rec"),
    "v5-mobile": ("PP-OCRv5_mobile_det", "PP-OCRv5_mobile_rec"),
    "ko-v6-medium-det": ("PP-OCRv6_medium_det", "korean_PP-OCRv5_mobile_rec"),
    "ko-v6-small-det": ("PP-OCRv6_small_det", "korean_PP-OCRv5_mobile_rec"),
    "ko-v5-server-det": ("PP-OCRv5_server_det", "korean_PP-OCRv5_mobile_rec"),
    "ko-v5-mobile-det": ("PP-OCRv5_mobile_det", "korean_PP-OCRv5_mobile_rec"),
}
# CPU 只量沒有 GPU 時的備案（準確度和 DirectML 相同，M0-14 已確認）
CPU_COMBOS = ("v6-small", "v6-tiny", "v5-mobile", "ko-v6-small-det", "ko-v5-mobile-det")


def images(category: str) -> list[Path]:
    return sorted(p for p in (PRIVATE / category).iterdir()
                  if p.is_file() and p.suffix.lower() in IMAGE_SUFFIXES)


def run(combo: str, category: str, device: str, force: bool) -> str:
    output = OUTPUT / device / combo / f"{category}.json"
    if output.exists() and not force:
        return "已有結果"
    detection, recognition = COMBOS[combo]
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".tmp")
    start = time.monotonic()
    result = subprocess.run(
        [str(OCR_CLI), "--det", str(MODELS / detection), "--rec", str(MODELS / recognition),
         "--device", device, "--repeat", "2", "--output", str(temporary),
         *map(str, images(category))],
        capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        raise SystemExit(f"{combo} {category} 失敗：\n{result.stdout}\n{result.stderr}")
    temporary.replace(output)
    return f"{time.monotonic() - start:.0f} 秒"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--device", choices=("dml", "cpu"), default="dml")
    parser.add_argument("--combos", nargs="+", choices=sorted(COMBOS))
    parser.add_argument("--categories", nargs="+")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    if not OCR_CLI.exists():
        raise SystemExit(f"找不到 {OCR_CLI}，先執行 cmake --workflow --preset release")
    combos = args.combos or (list(COMBOS) if args.device == "dml" else list(CPU_COMBOS))
    categories = args.categories or sorted(
        p.name for p in PRIVATE.iterdir() if p.is_dir() and images(p.name))
    for combo in combos:
        for category in categories:
            print(f"{args.device} {combo:18} {category:9} {run(combo, category, args.device, args.force)}",
                  flush=True)


if __name__ == "__main__":
    main()
