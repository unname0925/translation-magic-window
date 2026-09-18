# Translation Magic Window

一個 Windows 螢幕翻譯工具。把半透明的「透鏡」放在漫畫、遊戲或網頁上，透鏡底下的日文、英文、韓文會自動翻譯成繁體中文。

> **目前狀態**：開發初期（M0 技術驗證），還沒有可以使用的功能。

## 文件

- [設計文件](docs/design.md)：需求、架構、各模組的設計
- [執行計畫](docs/execution-plan.md)：待辦清單、里程碑、驗證機制

## 開發環境

- Windows 11
- [Build Tools for Visual Studio 2026](https://visualstudio.microsoft.com/downloads/)，安裝時勾選「使用 C++ 的桌面開發」
- CMake 3.28 以上
- VS Code 搭配 C/C++ Extension Pack（選用）

## 建置與測試

### 在 VS Code 中

1. 開啟專案資料夾。
2. `Ctrl+Shift+P` →`CMake: Select Configure Preset`，選擇 **MSVC x64**。
3. `Ctrl+Shift+P` →`CMake: Build`（或按 `F7`）。
4. `Ctrl+Shift+P` →`CMake: Run Tests`，或使用左側的「測試」面板。

### 在命令列中

設定、建置、執行測試，一個指令完成：

```bash
cmake --workflow --preset debug
```

其他流程：

| 指令 | 用途 |
|---|---|
| `cmake --workflow --preset release` | Release 版 |
| `cmake --workflow --preset asan` | 開啟 AddressSanitizer，檢查記憶體錯誤 |
| `cmake --workflow --preset integration` | 整合測試（透鏡、點擊穿透、拖動、螢幕擷取等），約 15 秒 |

建置結果在 `build/<preset>/bin/<設定>/`。

> 防毒軟體（例如 Norton）第一次執行新建置的程式時，可能會先掃描 20 秒以上，這是正常的。

**執行整合測試前後要注意：**

- 先從系統匣結束正在執行的 Translation Magic Window，因為測試要啟動自己的執行個體。
- 測試會短暫顯示測試視窗、啟動主程式，並**移動滑鼠和點擊**，執行期間請不要操作滑鼠。
  - 每次點擊前都會先確認位置是測試視窗或透鏡，不會點到其他程式。
  - 測試結束後，游標會回到原來的位置。
- 主程式使用暫存資料夾（`--data-dir`），不會在你的擷取資料夾中留下檔案。

## 下載模型

OCR 等模型不放在倉庫中，用腳本下載到 `models/`（需要 Python 3.10 以上，只用標準函式庫）：

```powershell
python tools/fetch_models/fetch_models.py --list          # 列出模型和大小
python tools/fetch_models/fetch_models.py                 # 下載全部（約 920 MB）
python tools/fetch_models/fetch_models.py --group ocr     # 只下載 OCR 模型（約 380 MB）
python tools/fetch_models/fetch_models.py --verify-only   # 只檢查已下載的檔案
```

每個檔案都固定在特定的版本，下載後會用大小和 SHA-256 驗證。已經下載而且驗證通過的檔案不會重新下載。

## 授權

[GPL-3.0](LICENSE)
