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

## manga-ocr 的 ONNX 版本（M0-15）

manga-ocr 用另一個環境 `tools/eval/.venv-manga`：它需要 transformers 4.x，而 transformers 4.x 要求 huggingface-hub < 1.0，會和 PaddleOCR 的環境衝突。

```powershell
python -m venv tools/eval/.venv-manga
tools/eval/.venv-manga/Scripts/python -m pip install -r tools/eval/requirements-manga.lock.txt
python tools/fetch_models/fetch_models.py --only manga-ocr-base
tools/eval/.venv-manga/Scripts/python tools/eval/check_manga_ocr.py
```

`check_manga_ocr.py` 會：

1. 產生合成的漫畫對話框（`make_manga_crops.py`，只放在 `build/manga_ocr/crops`）。
2. 第一次執行時把模型匯出成 `build/manga_ocr_onnx/encoder.onnx`、`decoder.onnx`。
3. 用官方 manga-ocr（PyTorch）產生參考答案。
4. 用 ONNX Runtime（CPU、DirectML）加上 `manga_onnx.py` 自己寫的 beam search，檢查產生的 token 和文字是否和官方完全相同。
5. 另外跑逐字解碼，記錄結果和速度；報告寫在 `build/manga_ocr/report.md`。

## 正確答案（M0-10）

真實截圖的正確答案放在 `testdata/private/<分類>/ground_truth.txt`（有版權，不進版本控制）。格式寫在 `ground_truth.py` 的開頭：一張截圖一段，每個區塊記錄種類、方向、字級、位置和文字，日文另外記錄ルビ（讀音，以及是一般讀音還是另有含義）。

**校對**（你）：用正確答案編輯器，直接在截圖上看框、改文字：

```powershell
tools/eval/.venv/Scripts/python tools/eval/gt_editor.py ja-manga
```

- 上面選分類和截圖；左邊是截圖和每個區塊的框（紅色：評測；灰色：不評測；藍色：選取中），右邊是區塊清單（順序就是閱讀順序）和選取區塊的欄位。
- 在空白處拖曳新增框（按住 Shift 可以在別的框裡面新增）；拖曳框移動，拖曳角或邊調整大小；滾輪縮放，右鍵拖曳移動畫面。
- 選取一個框後按 Ctrl+R（或「辨識文字」），會用 C++ 的 `tmw_ocr_cli` 辨識框裡的字填進去，再手動核對。需要先建置 release 版（`cmake --workflow --preset release`）並下載模型。
- Ctrl+S 存檔，存檔前會檢查格式，有問題會跳到那個區塊；第一次存檔時會把原本的檔案備份成 `ground_truth.txt.bak`。Ctrl+Z 復原框的新增、刪除、移動和辨識。
- 新加的截圖打開時沒有區塊，可以按「匯入草稿」加入模型產生的草稿（見下面），或直接框選再辨識。

`review_ground_truth.py` 一次檢查所有分類的格式，並在 `build/gt_review/<分類>/` 畫出每張截圖的區塊，方便快速瀏覽：

```powershell
tools/eval/.venv/Scripts/python tools/eval/review_ground_truth.py
```

**草稿怎麼來的**（Claude）：

1. 用 `tmw_ocr_cli` 跑每個分類，結果存成 `build/gt_drafts/<分類>.ppocr.json`（日文、英文用 PP-OCRv6 medium，韓文用 PP-OCRv5 server 偵測加韓文辨識模型）。
2. `draft_ground_truth.py` 把逐行結果合併成區塊、找出ルビ，日文漫畫另外用 manga-ocr 辨識，產生 `build/gt_drafts/<分類>/ground_truth.draft.txt` 和標出區塊編號的圖（必須用 `.venv-manga` 執行）。
3. 用 `show_draft.py <分類> <第幾張>` 印出草稿，逐張看圖修正文字、補上漏掉的區塊、刪掉誤判，寫成 `ground_truth.txt`。

## OCR 評測（M0-11）

用真實截圖和正確答案比較各種模型。結果都在 `build/ocr_eval/m0-11/`（含有截圖裡的文字，不進版本控制）。

```powershell
cmake --workflow --preset release                               # 建置 tmw_ocr_cli
python tools/eval/run_ocr.py --device dml                       # 所有組合跑一次（RTX 4070 約 12 分鐘）
python tools/eval/run_ocr.py --device cpu                       # 較小的組合在 CPU 上量耗時
powershell -NoProfile -ExecutionPolicy Bypass -File tools/eval/windows_ocr.ps1 -Category en-web
python tools/eval/evaluate_ocr.py                               # 評分，寫出 report.md
tools/eval/.venv-manga/Scripts/python tools/eval/evaluate_manga.py      # 日文漫畫：manga-ocr
tools/eval/.venv-manga/Scripts/python tools/eval/evaluate_detector.py   # 漫畫的文字偵測
```

- `run_ocr.py` 的結果存在 `raw/<裝置>/<組合>/<分類>.json`，已經有的會略過，所以改了正確答案只要重跑評分。
- 評分方式（行怎麼對應到區塊、ルビ怎麼去掉、各項指標）寫在 `evaluate_ocr.py` 的開頭。
  韓文的語言判斷策略是用同一個偵測模型的兩個辨識結果逐行模擬的（文字框完全相同）。
- `windows_ocr.ps1` 需要對應語言的 Windows OCR 語言套件，`-List` 可以查看已安裝的語言。
- `evaluate_manga.py` 用正確答案的框裁切日文漫畫的每個區塊（假設偵測完全正確），比較 manga-ocr 的逐字解碼、
  beam search 和 PP-OCR。裁切圖也可以拿來確認 ONNX 版在真實截圖上和官方版一致：
  `check_manga_ocr.py --crops build/ocr_eval/m0-11/crops/ja-manga`。
- `evaluate_detector.py` 比較 comic-text-detector 和 PP-OCR 的偵測模型各找到多少區塊（依種類、字級分開），
  並在 `detector/<分類>/` 畫出偵測結果。

## 翻譯評測（M0-12）

同一批原文（正確答案裡的每個區塊，去掉純數字和單一符號，共 838 段）送到各翻譯引擎，你再盲評打分。
結果都在 `build/translation_eval/m0-12/`（含有截圖裡的文字，不進版本控制）。

```powershell
tools/eval/.venv/Scripts/python tools/eval/translate.py --engines google gemini claude hy-mt2
tools/eval/.venv/Scripts/python tools/eval/rate_translations.py      # 你盲評打分
tools/eval/.venv/Scripts/python tools/eval/evaluate_translation.py   # 產生 report.md
```

- `google` 不需要金鑰；`gemini` 需要 `GEMINI_API_KEY`、`claude` 需要 `ANTHROPIC_API_KEY`
  （金鑰只從環境變數讀，不會寫進任何檔案；機構層級的 Anthropic 金鑰還要 `ANTHROPIC_WORKSPACE_ID`）。
- **不要浪費額度**：翻過而且原文沒變的段落不會重送（`--force` 才會），每翻完一頁就存檔，
  中途停掉可以接著跑。付費引擎會累計 token 並估算費用，超過 `--budget-usd`（預設 2 美元）就停下來存檔。
  免費的雲端 LLM 額度很小（M0-12 時 Gemini 3.6 flash 每天只有 20 次請求），被限流時用 `--interval`
  放慢，或用 `--model` 換一個額度比較寬的模型。
- 本機模型（`hy-mt2`）要先下載並匯入 Ollama：

  ```powershell
  python tools/fetch_models/fetch_models.py --manifest tools/eval/models-eval.json
  ollama create hy-mt2 -f tools/eval/Modelfile.hy-mt2
  ```

  `models-eval.json` 是評測專用的模型清單（正式程式不內建本機 LLM，所以不放進
  `tools/fetch_models/models.json`），用的是同一支會驗證 SHA-256 的下載腳本。
- 譯文都經過 OpenCC 的 `s2twp`（和產品相同）。另有含義的ルビ用 `{本文|讀音}` 送出，
  看引擎能不能保留標記（design.md 4.5）。
- 本機模型一次只翻一段、輸出很短，GPU 大半在等（解碼受限於記憶體頻寬），所以同一頁的段落會同時送出
  （`--parallel`，預設 8）。另外**一定要連 `127.0.0.1` 而不是 `localhost`**：Windows 上 `localhost`
  會先試 IPv6 再退回 IPv4，每個請求多花約 2 秒（實測每頁 6.9 秒 → 1.6 秒）。
- `rate_translations.py` 每個分類抽 6 段（依 key 的雜湊，結果固定），含特殊ルビ的一定會抽到；
  各引擎的譯文打亂順序、不顯示引擎名稱，用 1～5 打分，每打一次就存檔，可以隨時關掉再繼續。
- `evaluate_translation.py` 把分數對照回引擎，另外自動檢查：失敗、譯文裡還有假名或韓文字母、
  轉換後還有簡體字、ルビ標記有沒有保留、每段耗時。

## 各檔案

| 檔案 | 用途 |
|---|---|
| `make_synthetic_images.py` | 產生合成圖片（英、日、韓、直排、旋轉、遊戲對話框、透鏡大小的畫面） |
| `ocr_reference.py` | PaddleOCR 官方實作的結果（參考答案） |
| `compare_ocr.py` | 比對兩份結果（只用標準函式庫） |
| `check_ocr_equivalence.py` | 上面全部串起來，所有模型組合一次跑完 |
| `manga_onnx.py` | manga-ocr 的 ONNX 匯出、前處理、beam search 和逐字解碼、轉成文字 |
| `make_manga_crops.py` | 產生合成的漫畫對話框（直排、橫排、重複的狀聲詞、網點、小字） |
| `check_manga_ocr.py` | manga-ocr 的 ONNX 版本和官方版的一致性檢查 |
| `ground_truth.py` | 正確答案的格式：讀取、寫出、比較文字前的正規化（只用標準函式庫） |
| `draft_ground_truth.py` | 從模型的辨識結果產生正確答案的草稿 |
| `show_draft.py` | 印出某一張截圖的草稿 |
| `gt_editor.py` | 正確答案編輯器（視窗）：框選、輸入文字、用 OCR 辨識框裡的字 |
| `review_ground_truth.py` | 檢查正確答案，畫出校對用的圖 |
| `ocr_lines.py` | 逐行辨識結果的共用處理：找出ルビ、排出閱讀順序（只用標準函式庫） |
| `run_ocr.py` | 用 `tmw_ocr_cli` 對真實截圖跑每一種模型組合 |
| `windows_ocr.ps1` | 用 Windows 內建的 OCR 辨識真實截圖，輸出格式和 `tmw_ocr_cli` 相同 |
| `evaluate_ocr.py` | OCR 評分和報告，包含韓文的語言判斷策略（只用標準函式庫） |
| `evaluate_manga.py` | 日文漫畫：manga-ocr（逐字解碼、beam search）和 PP-OCR 的比較 |
| `comic_text_detector.py` | comic-text-detector（ONNX）的推論和後處理 |
| `evaluate_detector.py` | 漫畫文字偵測率：comic-text-detector 和 PP-OCR 的比較 |
| `translate.py` | M0-12：把原文送到各翻譯引擎（Google、Gemini、本機 Ollama） |
| `rate_translations.py` | 翻譯盲評的視窗（打亂、不顯示引擎名稱） |
| `evaluate_translation.py` | 翻譯評測的報告：盲評分數和自動檢查 |
| `models-eval.json`、`Modelfile.hy-mt2` | 評測專用模型的下載清單，以及匯入 Ollama 的設定 |

C++ 的命令列工具 `tmw_ocr_cli`（`tools/ocr_cli/`）也可以單獨使用：

```powershell
build/msvc-x64/bin/Release/tmw_ocr_cli.exe --det models/PP-OCRv6_medium_det `
    --rec models/PP-OCRv6_medium_rec --device dml --repeat 5 --output result.json image.png
```
