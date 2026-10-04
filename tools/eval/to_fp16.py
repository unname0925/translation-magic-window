"""把模型轉成 fp16（輸入輸出維持 fp32，C++ 不用改），省顯示記憶體也比較快。只在 DirectML 上用。

量測（RTX 4070，DirectML，每個模型一個程序，tools/eval 之外的暫存腳本）：
- comic-text-detector（1024×1024）：顯示記憶體 796 → 411 MB、推論 39 → 15 ms。
  私人測試集 20 頁（日、韓）111 個對話框全部對得上（IoU 最差 0.935），分段後的文字 20 頁完全相同。
- manga-ocr 編碼器：顯示記憶體 417 → 247 MB、每張 4.3 → 2.6 ms；124 個直排對白有 123 個讀出完全相同
  （另一個差一個字，字元錯誤率見 docs/proposal-speed-and-web-manga.md）。
- PP-OCRv6 medium 偵測：私人測試集 9 個分類（日英韓 × 遊戲／網頁／漫畫）的字元錯誤率和 fp32 幾乎相同
  （日文漫畫 15.23 → 15.16%、韓文遊戲 3.82 → 3.89%，其餘完全相同）。
- PP-OCR 辨識（medium、韓文 mobile）：轉 fp16 會壞掉（英文遊戲 0.54 → 10.9%、韓文 3.8 → 100%），不採用。
- LaMa：直接轉整張輸出都是 255（傅立葉單元溢位）；傅立葉單元留在 fp32 之後顯示記憶體 1074 → 464 MB，
  但遮罩內和 fp32 差很多（PSNR 12.4 dB），不採用。

    tools/eval/.venv-manga/Scripts/python tools/eval/to_fp16.py comic-text-detector

主程式在 DirectML 上找到 fp16 檔就用它，沒有時照舊用 fp32（ocr/ocr_service.cpp）。
需要 onnx 和 onnxruntime 套件（.venv-manga 都有；轉換器是 onnxruntime 附的 onnxruntime.transformers.float16）。
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MODELS = {
    "comic-text-detector": (REPO / "models/comic-text-detector/comictextdetector.pt.onnx",
                            REPO / "models/comic-text-detector/comictextdetector.fp16.onnx"),
    "manga-ocr-encoder": (REPO / "models/manga-ocr/encoder.onnx",
                          REPO / "models/manga-ocr/encoder.fp16.onnx"),
    "PP-OCRv6_medium_det": (REPO / "models/PP-OCRv6_medium_det/inference.onnx",
                            REPO / "models/PP-OCRv6_medium_det/inference.fp16.onnx"),
}


def convert(source: Path, target: Path) -> None:
    import onnx
    from onnxruntime.transformers.float16 import convert_float_to_float16

    model = onnx.load(source)
    half = convert_float_to_float16(model, keep_io_types=True)
    onnx.save(half, target)
    print(f"{target}：{target.stat().st_size / 2**20:.0f} MB（原本 {source.stat().st_size / 2**20:.0f} MB）")


def main(argv: list[str]) -> int:
    names = argv or list(MODELS)
    for name in names:
        if name not in MODELS:
            print(f"不認得 {name}：可以用 {', '.join(MODELS)}")
            return 2
        source, target = MODELS[name]
        if not source.exists():
            print(f"找不到 {source}：先執行 tools/fetch_models")
            return 1
        convert(source, target)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
