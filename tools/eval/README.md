# tools/eval：OCR 評測與 C++／Python 一致性檢查

## 建立 Python 環境（第一次）

需要 Python 3.10 以上。環境建在 `tools/eval/.venv`（不進版本控制），不影響系統的 Python：

```powershell
python -m venv tools/eval/.venv
tools/eval/.venv/Scripts/python -m pip install -r tools/eval/requirements.lock.txt
```

- `requirements.txt`：直接使用的套件（PaddleOCR 3.7.0、ONNX Runtime DirectML 版 1.24.4）。
- `requirements.lock.txt`：實際安裝的完整版本清單，用它安裝才能重現相同的環境。

PaddleOCR 用 ONNX Runtime 執行（`engine="onnxruntime"`），不需要安裝 PaddlePaddle。

## C++ 和 Python 的一致性檢查（M0-14）

C++ 版的 OCR（`src/ocr/`）逐步照著 PaddleOCR 官方實作（PaddleX 3.7）寫，同一張圖片的結果必須和官方版一致。

```powershell
cmake --workflow --preset release                               # 建置 tmw_ocr_cli
python tools/fetch_models/fetch_models.py --group ocr           # 下載模型
tools/eval/.venv/Scripts/python tools/eval/check_ocr_equivalence.py
```

它會：

1. 產生合成圖片（`make_synthetic_images.py`，用 Windows 內建字型，只放在 `build/ocr_eval/images`）。
2. 逐字比對每個辨識模型的字元表（C++ 的 yaml-cpp 和 Python 的 PyYAML 讀到的必須相同）。
3. 每個模型組合都用三種方式跑：Python 參考版（`ocr_reference.py`，CPU）、C++ CPU、C++ DirectML。
4. 用 `compare_ocr.py` 比對：文字完全相同，文字框每個角的誤差不超過 2px。
5. 把 C++ 的耗時整理成 `build/ocr_eval/report.md`。

## 各檔案

| 檔案 | 用途 |
|---|---|
| `make_synthetic_images.py` | 產生合成圖片（英、日、韓、直排、旋轉、遊戲對話框、透鏡大小的畫面） |
| `ocr_reference.py` | PaddleOCR 官方實作的結果（參考答案） |
| `compare_ocr.py` | 比對兩份結果（只用標準函式庫） |
| `check_ocr_equivalence.py` | 上面全部串起來，所有模型組合一次跑完 |

C++ 的命令列工具 `tmw_ocr_cli`（`tools/ocr_cli/`）也可以單獨使用：

```powershell
build/msvc-x64/bin/Release/tmw_ocr_cli.exe --det models/PP-OCRv6_medium_det `
    --rec models/PP-OCRv6_medium_rec --device dml --repeat 5 --output result.json image.png
```
