"""OCR 回歸評測：在公開的合成測試集上跑 C++ 的 OCR，和基準線比（docs/execution-plan.md 5.5、M2-12）。

    python tools/eval/check_synthetic.py --ocr-cli build/ci/bin/Release/tmw_ocr_cli.exe \\
        --models models --variant medium [--device cpu] [--update]

- 每張圖依 expected.json 的語言挑辨識模型（韓文用 korean_PP-OCRv5_mobile_rec），
  --variant 決定偵測和日文／英文辨識用 PP-OCRv6 的 medium 還是 small（產品在 GPU 上用 medium、
  CPU 上用 small，見 ocr/model_choice.h）。
- 字元錯誤率不看行的順序（閱讀順序和分段另外有單元測試和 evaluate_layout.py）：
  正確答案和 OCR 結果都去掉空白、做 NFKC 正規化（全形數字和半形數字視為相同），
  錯誤數 = max(正確字數, 讀到的字數) − 兩邊共同的字數（以多重集合計）。
  換錯一個字、漏一個字、多一個字都算一個錯誤。
- 比基準線（baseline.json）差超過 1 個百分點就失敗（5.5 的判定標準）。確實變好時用 --update 更新。

只用 Python 標準函式庫，CI 不必另外安裝套件。
"""

from __future__ import annotations

import argparse
import collections
import json
import subprocess
import sys
import tempfile
import unicodedata
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SYNTHETIC = REPO_ROOT / "testdata" / "synthetic"
TOLERANCE = 0.01  # 比基準線差超過 1 個百分點才算退步


def normalise(text: str) -> str:
    return "".join(c for c in unicodedata.normalize("NFKC", text) if not c.isspace())


def character_error_rate(expected: list[str], read: list[str]) -> float:
    truth = normalise("".join(expected))
    ours = normalise("".join(read))
    shared = sum((collections.Counter(truth) & collections.Counter(ours)).values())
    return (max(len(truth), len(ours)) - shared) / max(1, len(truth))


def run_ocr(ocr_cli: Path, models: Path, variant: str, device: str, recognizer: str,
            images: list[Path]) -> dict[str, list[str]]:
    """跑一次 ocr_cli，回傳 圖檔名 → 讀到的每一行。"""
    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary) / "result.json"
        command = [str(ocr_cli), "--det", str(models / f"PP-OCRv6_{variant}_det"),
                   "--rec", str(models / recognizer), "--device", device,
                   "--output", str(output), *map(str, images)]
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
        result = json.loads(output.read_text(encoding="utf-8"))
    return {Path(image["image"]).name: [line["text"] for line in image["lines"]]
            for image in result["images"]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ocr-cli", type=Path, required=True)
    parser.add_argument("--models", type=Path, default=REPO_ROOT / "models")
    parser.add_argument("--variant", choices=["medium", "small"], required=True)
    parser.add_argument("--device", default="cpu", choices=["cpu", "dml"])
    parser.add_argument("--update", action="store_true", help="把這次的結果寫成新的基準線")
    args = parser.parse_args()

    expected = json.loads((SYNTHETIC / "expected.json").read_text(encoding="utf-8"))
    by_recognizer = collections.defaultdict(list)
    for name, entry in expected.items():
        recognizer = ("korean_PP-OCRv5_mobile_rec" if entry["language"] == "ko"
                      else f"PP-OCRv6_{args.variant}_rec")
        by_recognizer[recognizer].append(SYNTHETIC / name)
    read: dict[str, list[str]] = {}
    for recognizer, images in by_recognizer.items():
        read.update(run_ocr(args.ocr_cli, args.models, args.variant, args.device, recognizer,
                            images))

    key = f"{args.device}/{args.variant}"
    baseline_path = SYNTHETIC / "baseline.json"
    baseline = json.loads(baseline_path.read_text(encoding="utf-8")) if baseline_path.exists() else {}
    previous = baseline.get(key, {})
    scores, failures = {}, []
    for name, entry in expected.items():
        rate = character_error_rate(entry["lines"], read.get(name, []))
        scores[name] = round(rate, 4)
        before = previous.get(name)
        verdict = ""
        if before is not None and rate > before + TOLERANCE:
            failures.append(name)
            verdict = f"  ← 退步（基準 {before:.1%}）"
        print(f"{name:22} {rate:6.1%}{verdict}")
        if rate > 0 and (verdict or args.update or before is None):
            print(f"{'':22} 讀到：{' / '.join(read.get(name, []))}")

    if args.update:
        baseline[key] = scores
        baseline_path.write_text(json.dumps(baseline, ensure_ascii=False, indent=2, sort_keys=True)
                                 + "\n", encoding="utf-8", newline="\n")
        print(f"已更新 {baseline_path} 的 {key}")
        return 0
    if not previous:
        print(f"baseline.json 裡還沒有 {key}，用 --update 建立", file=sys.stderr)
        return 2
    if failures:
        print(f"{len(failures)} 張圖的字元錯誤率比基準線差超過 {TOLERANCE:.0%}："
              + "、".join(failures), file=sys.stderr)
        return 1
    print(f"{key}：{len(scores)} 張圖都沒有退步")
    return 0


if __name__ == "__main__":
    sys.exit(main())
