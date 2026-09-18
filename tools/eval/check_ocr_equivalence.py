"""M0-14 的驗收：每個模型組合都用 Python 參考版（PaddleOCR，CPU）、C++ CPU、C++ DirectML
各跑一次，檢查 C++ 的結果和參考版一致（文字完全相同、文字框誤差 ≤ 2px），並整理 C++ 的耗時。
另外逐字比對每個辨識模型的字元表（C++ 用 yaml-cpp、Python 用 PyYAML 讀 inference.yml）。

必須用 tools/eval/.venv 的 Python 執行（參考版需要 paddleocr）：
    tools/eval/.venv/Scripts/python tools/eval/check_ocr_equivalence.py

需要：models/（tools/fetch_models）、Release 版的 tmw_ocr_cli（cmake --workflow --preset release）。
結果寫在 build/ocr_eval/：每次執行的 JSON 和 report.md。
"""

from __future__ import annotations

import argparse
import json
import statistics
import subprocess
import sys
from pathlib import Path

import compare_ocr
import yaml

REPO_ROOT = Path(__file__).resolve().parents[2]
EVAL_DIR = Path(__file__).resolve().parent
OUTPUT_DIR = REPO_ROOT / "build" / "ocr_eval"

# 名稱、偵測模型、辨識模型
COMBINATIONS = [
    ("v6_medium", "PP-OCRv6_medium_det", "PP-OCRv6_medium_rec"),
    ("v6_small", "PP-OCRv6_small_det", "PP-OCRv6_small_rec"),
    ("v6_tiny", "PP-OCRv6_tiny_det", "PP-OCRv6_tiny_rec"),
    ("v5_server", "PP-OCRv5_server_det", "PP-OCRv5_server_rec"),
    ("v5_mobile", "PP-OCRv5_mobile_det", "PP-OCRv5_mobile_rec"),
    ("korean", "PP-OCRv5_server_det", "korean_PP-OCRv5_mobile_rec"),
]


def run(command: list[str]) -> None:
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                            errors="replace")
    if result.returncode != 0:
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(command)}\n"
                           f"{result.stdout}\n{result.stderr}")


def dictionary_problems(cli: Path, rec_dir: Path, output: Path) -> list[str]:
    """C++ 讀到的 CTC 字元表必須和 PaddleX（PyYAML）完全相同。"""
    run([str(cli), "--rec", str(rec_dir), "--dump-characters", str(output)])
    cpp = json.loads(output.read_text(encoding="utf-8"))
    config = yaml.safe_load((rec_dir / "inference.yml").read_text(encoding="utf-8"))
    entries = config["PostProcess"]["character_dict"]
    problems = [f"{rec_dir.name}: entry {i} is not a string: {e!r}"
                for i, e in enumerate(entries) if not isinstance(e, str)]
    expected = ["blank", *entries, " "]
    if len(cpp) != len(expected):
        problems.append(f"{rec_dir.name}: {len(cpp)} characters, expected {len(expected)}")
    problems += [f"{rec_dir.name}: character {i} is {b!r}, expected {a!r}"
                 for i, (a, b) in enumerate(zip(expected, cpp)) if a != b][:10]
    return problems


def timing_summary(result: dict) -> dict:
    images = result["images"]
    lines = sum(len(image["lines"]) for image in images)
    detection = [image["timings_ms"]["detection"] for image in images]
    recognition = [image["timings_ms"]["recognition"] for image in images]
    return {
        "detection_ms": statistics.mean(detection),
        "recognition_ms": statistics.mean(recognition),
        "recognition_per_line_ms": sum(recognition) / max(1, lines),
        "load_ms": result["model_load_ms"],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cli", type=Path,
                        default=REPO_ROOT / "build/msvc-x64/bin/Release/tmw_ocr_cli.exe")
    parser.add_argument("--models", type=Path, default=REPO_ROOT / "models")
    parser.add_argument("--images", type=Path, default=OUTPUT_DIR / "images")
    # C++ 每張圖片跑幾次（第一次暖機，耗時取其餘的中位數）。CPU 預設少跑幾次：
    # PP-OCRv5 server 在 CPU 上每張圖片要將近一分鐘
    parser.add_argument("--cpu-repeat", type=int, default=2)
    parser.add_argument("--dml-repeat", type=int, default=5)
    parser.add_argument("--reuse-reference", action="store_true",
                        help="已經有 Python 參考結果時直接沿用（模型和圖片沒變時才用）")
    parser.add_argument("--only", nargs="+", metavar="NAME", help="只跑這些組合")
    args = parser.parse_args()

    if not args.cli.exists():
        print(f"error: {args.cli} not found; build it with cmake --workflow --preset release")
        return 2
    if not args.images.exists():
        run([sys.executable, str(EVAL_DIR / "make_synthetic_images.py"), "--output",
             str(args.images)])
    images = sorted(str(p) for p in args.images.glob("*.png"))
    results_dir = OUTPUT_DIR / "results"
    results_dir.mkdir(parents=True, exist_ok=True)

    combinations = [c for c in COMBINATIONS if not args.only or c[0] in args.only]
    rows = []
    failures = 0
    for rec in sorted({c[2] for c in combinations}):
        problems = dictionary_problems(args.cli, args.models / rec,
                                       results_dir / f"{rec}_characters.json")
        print(f"== dictionary {rec}: {'PASS' if not problems else 'FAIL'}")
        for problem in problems:
            print(f"   {problem}")
        failures += len(problems)
    for name, det, rec in combinations:
        det_dir, rec_dir = args.models / det, args.models / rec
        reference_json = results_dir / f"{name}_python_cpu.json"
        print(f"== {name}: {det} + {rec}")
        if not (args.reuse_reference and reference_json.exists()):
            run([sys.executable, str(EVAL_DIR / "ocr_reference.py"), "--det", str(det_dir),
                 "--rec", str(rec_dir), "--output", str(reference_json), *images])
        reference = compare_ocr.load(reference_json)
        row = {"name": name, "det": det, "rec": rec}
        for device in ("cpu", "dml"):
            output = results_dir / f"{name}_cpp_{device}.json"
            repeat = args.cpu_repeat if device == "cpu" else args.dml_repeat
            run([str(args.cli), "--det", str(det_dir), "--rec", str(rec_dir), "--device", device,
                 "--repeat", str(repeat), "--output", str(output), *images])
            candidate = compare_ocr.load(output)
            problems, stats = compare_ocr.compare(reference, candidate, box_tolerance=2)
            row[device] = {"problems": problems, "stats": stats,
                           "timings": timing_summary(candidate)}
            status = "PASS" if not problems else f"FAIL ({len(problems)} problems)"
            print(f"   cpp {device}: {status}, {stats['lines']} lines, "
                  f"max box error {stats['max_box_error']}px, "
                  f"max score diff {stats['max_score_diff']:.6f}")
            for problem in problems:
                print(f"      {problem}")
            failures += len(problems)
        rows.append(row)

    report = ["| 組合 | 行數 | CPU 一致 | DirectML 一致 | 框最大誤差 | "
              "偵測 CPU／DML (ms) | 辨識每行 CPU／DML (ms) | 載入 CPU／DML (ms) |",
              "|---|---|---|---|---|---|---|---|"]
    for row in rows:
        cpu, dml = row["cpu"], row["dml"]
        report.append(
            f"| {row['name']} | {cpu['stats']['lines']} "
            f"| {'✅' if not cpu['problems'] else '❌'} "
            f"| {'✅' if not dml['problems'] else '❌'} "
            f"| {max(cpu['stats']['max_box_error'], dml['stats']['max_box_error'])}px "
            f"| {cpu['timings']['detection_ms']:.1f}／{dml['timings']['detection_ms']:.1f} "
            f"| {cpu['timings']['recognition_per_line_ms']:.1f}／"
            f"{dml['timings']['recognition_per_line_ms']:.1f} "
            f"| {cpu['timings']['load_ms']:.0f}／{dml['timings']['load_ms']:.0f} |")
    (OUTPUT_DIR / "report.md").write_text("\n".join(report) + "\n", encoding="utf-8")
    print("\n" + "\n".join(report))
    print(f"\n{'PASS' if failures == 0 else 'FAIL'}: {failures} problem(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
