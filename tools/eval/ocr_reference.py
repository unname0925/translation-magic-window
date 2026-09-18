"""PaddleOCR 官方實作（Python）的 OCR 結果，作為 C++ 版（tmw_ocr_cli）的比對基準。

用 ONNX Runtime 執行和 C++ 相同的模型檔，參數也相同（PaddleOCR OCR 管線的預設值），
並關閉 C++ 沒有實作的步驟（文件方向校正、文件展平、文字行方向分類）。
辨識的批次大小固定為 1：批次中的圖片會補齊成相同寬度，會影響結果，C++ 也是一次一張。

用法：
    tools/eval/.venv/Scripts/python tools/eval/ocr_reference.py \
        --det models/PP-OCRv6_medium_det --rec models/PP-OCRv6_medium_rec \
        --output build/ocr_eval/results/python_cpu_v6.json build/ocr_eval/images/*.png
"""

from __future__ import annotations

import argparse
import json
import os
import time
from pathlib import Path

# 不要在啟動時連線檢查模型下載來源：模型都用本機的檔案
os.environ.setdefault("PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK", "True")

import yaml  # noqa: E402
from paddleocr import PaddleOCR  # noqa: E402


def model_name(model_dir: Path) -> str:
    config = yaml.safe_load((model_dir / "inference.yml").read_text(encoding="utf-8"))
    return config["Global"]["model_name"]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--det", type=Path, required=True)
    parser.add_argument("--rec", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("images", type=Path, nargs="+")
    args = parser.parse_args()

    load_start = time.perf_counter()
    ocr = PaddleOCR(
        text_detection_model_name=model_name(args.det),
        text_detection_model_dir=str(args.det),
        text_recognition_model_name=model_name(args.rec),
        text_recognition_model_dir=str(args.rec),
        use_doc_orientation_classify=False,
        use_doc_unwarping=False,
        use_textline_orientation=False,
        text_recognition_batch_size=1,
        engine="onnxruntime",
        device="cpu",
    )
    load_ms = (time.perf_counter() - load_start) * 1000

    images = []
    for path in args.images:
        start = time.perf_counter()
        result = list(ocr.predict(str(path)))[0]
        elapsed_ms = (time.perf_counter() - start) * 1000
        height, width = result["doc_preprocessor_res"]["output_img"].shape[:2]
        lines = [
            {
                "box": [[int(x), int(y)] for x, y in poly],
                "text": text,
                "score": float(score),
            }
            for poly, text, score in zip(result["rec_polys"], result["rec_texts"],
                                         result["rec_scores"])
        ]
        images.append({
            "image": path.name,
            "width": int(width),
            "height": int(height),
            "lines": lines,
            "timings_ms": {"total": elapsed_ms},
        })
        print(f"{path.name}: {len(lines)} lines, {elapsed_ms:.1f} ms")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({
        "implementation": "python-paddleocr",
        "device": "cpu",
        "detection_model": args.det.name,
        "recognition_model": args.rec.name,
        "model_load_ms": load_ms,
        "images": images,
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
