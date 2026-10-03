# 網頁漫畫擴充功能（Chrome／Edge，原型）

在瀏覽器裡看漫畫時，按一下工具列的按鈕，整章的圖片都會翻成繁體中文、直接蓋在原圖上，
不用停下來等。翻譯由電腦上的 Translation Magic Window 做（OCR、manga-ocr、翻譯引擎、背景修補都沿用），
擴充功能只負責找圖、送圖、把譯文畫回網頁。設計見 `docs/proposal-speed-and-web-manga.md` 第二部分。

```
擴充功能 ──Native Messaging──▶ tmw_web_host.exe ──具名管道──▶ Translation Magic Window
 （content.js 找圖、畫譯文；       （只轉送，主程式沒開時       （一張一張排進處理管線，
   background.js 抓圖、送圖）        把它叫起來）                   一律當漫畫處理）
```

## 開發時怎麼裝

1. 建置 `tmw_web_host`（和主程式一起建置就有）。
2. 註冊主機（寫入目前使用者的 Chrome、Edge 登錄機碼；`--unregister` 移除）：
   ```
   python tools/web_extension/register_host.py
   ```
3. 瀏覽器打開 `chrome://extensions`（Edge 是 `edge://extensions`），打開「開發人員模式」，
   按「載入未封裝項目」，選這個 `extension` 資料夾。
   manifest 裡有開發用的公鑰，所以擴充功能 ID 固定是 `iohilhlbinnplmnnlipmamjenegjdggj`
   （私鑰在 `.cache/extension`，不進版本控制）。
4. 打開漫畫頁，按工具列的按鈕。第一次會問要不要讓它讀取所有網站的資料（抓跨網域的圖片要用）。
   再按一次切換原文／譯文。

## 目前能做的

- 找出頁面上的漫畫圖（`<img>`、畫好而且讀得到像素的 `<canvas>`），只挑主要內容欄；連到別的網站的圖當成廣告略過
- 依離畫面多近決定順序，同時交給主程式兩張（主程式一次處理一張）
- 延遲載入、無限捲動、單頁應用換章：新出現或換掉的圖自動補上
- 譯文畫在 shadow DOM 裡的一層 HTML，跟著圖片縮放；直排、描邊、背景修補的小圖、ルビ
- 同一張圖（內容相同）這次瀏覽器執行期間只翻一次

## 還沒做的

- 讀不到像素的 canvas（例如打亂拼圖再畫出來、而且被污染的閱讀器）改用畫面擷取
- 切成很多條的長條漫畫拼回一頁再偵測；一個對話框跨兩張圖
- 譯文的磁碟快取（重新整理之後還記得）、整章一起送翻譯以統一用詞
- 依網站記住「這張不要翻」
- 安裝程式自動註冊主機；上架「不公開」項目
