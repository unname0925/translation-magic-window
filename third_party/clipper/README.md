# Clipper 6.4.2

多邊形擴張（unclip）用的函式庫，授權為 Boost Software License 1.0（見 `clipper.hpp` 開頭）。

來源是 pyclipper 1.4.0 內附的版本，也就是 PaddleOCR 的 Python 版實際使用的程式碼，兩邊的文字框才會完全一致：

- 倉庫：https://github.com/fonttools/pyclipper
- commit：`f998920369565dca56d346bb30c5741e273f0b9b`（tag 1.4.0）
- `src/clipper.cpp`：SHA-256 `5c642a3668311701f72572443aa42c1a981edb037298efc015166d9d90be0755`
- `src/clipper.hpp`：SHA-256 `734eba9dc9d399089b2b467017074bd24728a1b9e64c7429e827806ed10e54cc`

pyclipper 編譯時沒有定義任何巨集（`use_lines`、`use_int32` 等），這裡也一樣。請不要修改這兩個檔案。
