"""網頁漫畫擴充功能 content.js 的行為測試：在無頭 Chrome 裡跑，不需要載入擴充功能、不連網。

測試頁用假的 chrome.runtime 回傳翻譯結果，放一欄延遲載入的「漫畫頁」和一個連到別的網站的廣告，檢查：
延遲載入的頁一開始就送出、廣告不送、譯文對齊（包括捲動、上面插進新圖之後）、元素重建時譯文立刻回來、
被網頁拿掉的譯文補回去、控制視窗的狀態／切換／停止。另外用「瀏覽器不支援 CSS anchor positioning」
再跑一次，檢查 JS 對齊的備用路線。

    python tools/web_extension/test_content.py
"""

from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CHROME = Path("C:/Program Files/Google/Chrome/Application/chrome.exe")


def make_page(content: str) -> str:
    PIXEL = "data:image/gif;base64,R0lGODlhAQABAAAAACH5BAEKAAEALAAAAAABAAEAAAICTAEAOw=="

    page = f"""<!doctype html><meta charset=utf-8>
    <style>
      body {{ margin: 0; }}
      #reader {{ position: relative; width: 640px; margin: 0 auto; }}
      #reader img {{ display: block; width: 600px; height: 900px; margin: 10px auto; }}
      .ad {{ position: fixed; right: 0; top: 0; width: 150px; height: 400px; }}
    </style>
    <a class=ad href="https://ads.example.invalid/"><img src="{PIXEL}" data-src="https://ads.example.invalid/banner.jpg" style="width:150px;height:400px"></a>
    <div id=reader></div>
    <script>
    const reader = document.getElementById("reader");
    for (let i = 0; i < 12; i++) {{
      const img = document.createElement("img");
      img.src = "{PIXEL}";               // 佔位圖
      img.dataset.src = "https://cdn.example.invalid/page" + i + ".jpg";  // 真正的網址
      reader.append(img);
    }}
    const requests = [];
    let listener = null;
    window.chrome = {{ runtime: {{ onMessage: {{ addListener: (f) => listener = f, removeListener: () => listener = null }},
                                  sendMessage: async (message) => {{
      requests.push(message.url || "inline");
      await new Promise((r) => setTimeout(r, 50));
      return {{ type: "result", id: message.url, width: 600, height: 900, error: "", patchesDropped: 0,
               items: [{{ rect: [100, 100, 300, 400], text: "譯文" + message.url.slice(-6), vertical: true,
                          foreground: "#000000", background: "#ffffff", size: "normal", lineThickness: 30, ruby: [] }}] }};
    }} }} }};
    </script>
    <script>{content}</script>
    <script>
    const report = [];
    function check(name, ok, detail = "") {{ report.push((ok ? "PASS " : "FAIL ") + name + (detail ? " (" + detail + ")" : "")); }}
    function overlays() {{ return [...document.querySelectorAll("tmw-overlay")]; }}
    function misalignment() {{
      let worst = 0;
      for (const anchor of overlays()) {{
        const img = anchor.previousElementSibling;
        const a = anchor.getBoundingClientRect(), r = img.getBoundingClientRect();
        worst = Math.max(worst, Math.abs(a.left - r.left), Math.abs(a.top - r.top), Math.abs(a.width - r.width), Math.abs(a.height - r.height));
      }}
      return worst;
    }}
    setTimeout(() => {{
      check("所有延遲載入的頁都在一開始就送出（不必捲動）", requests.filter(u => u.includes("cdn.")).length === 12, requests.length + " requests");
      check("廣告（連到別的網站）沒送", !requests.some(u => u.includes("ads.")));
      check("12 張都蓋上譯文", overlays().length === 12, overlays().length + " overlays");
      check("譯文和圖片對齊", misalignment() < 1, "worst " + misalignment().toFixed(2) + "px");
      scrollTo(0, 5000);
      setTimeout(() => {{
        check("捲動後仍對齊（不靠 JS 追位置）", misalignment() < 1, "worst " + misalignment().toFixed(2) + "px");
        // 模擬虛擬清單：第 3 張被拿掉、再建一個新的元素
        const old = reader.children[2];
        const url = old.dataset.src;
        old.nextElementSibling?.tagName === "TMW-OVERLAY" && old.nextElementSibling.remove();
        old.remove();
        const before = requests.length;
        setTimeout(() => {{
          const fresh = document.createElement("img");
          fresh.src = "{PIXEL}"; fresh.dataset.src = url;
          reader.insertBefore(fresh, reader.children[2]);
          setTimeout(() => {{
            check("元素重建後譯文立刻回來、不重新翻譯",
                  fresh.nextElementSibling?.tagName === "TMW-OVERLAY" && requests.length === before,
                  "requests " + before + " → " + requests.length);
            check("重建後仍對齊", misalignment() < 1, "worst " + misalignment().toFixed(2) + "px");
            // 網頁的程式把我們的元素拿掉（框架重繪）
            overlays()[0].remove();
            setTimeout(() => {{
              check("被網頁拿掉的譯文會補回去", overlays().length === 12, overlays().length + " overlays");
              let reply = null;
              listener({{ kind: "page-status" }}, {{}}, (r) => reply = r);
              check("控制視窗：狀態", reply && reply.active && reply.total === 12 && reply.done === 12 && reply.failed === 0, JSON.stringify(reply));
              listener({{ kind: "toggle" }}, {{}}, (r) => reply = r);
              check("控制視窗：切換原文", reply.visible === false);
              listener({{ kind: "stop" }}, {{}}, (r) => reply = r);
              check("控制視窗：停止後譯文和狀態列都拿掉", overlays().length === 0 && !document.querySelector("tmw-status") && !window.__tmwWebManga,
                    overlays().length + " overlays");
              check("停止後不留 anchor-name", ![...document.images].some((i) => i.style.getPropertyValue("anchor-name")));
              document.body.setAttribute("data-report", report.join(" | "));
              document.title = "DONE";
            }}, 1500);
          }}, 600);
        }}, 1200);
      }}, 600);
    }}, 2500);
    </script>"""
    return page


def run(page: str, folder: Path, name: str) -> list[str]:
    path = folder / f"{name}.html"
    path.write_text(page, encoding="utf-8")
    dom = subprocess.run(
        [str(CHROME), "--headless=new", "--disable-gpu", "--no-first-run",
         f"--user-data-dir={folder / 'profile'}", "--virtual-time-budget=30000",
         "--window-size=1280,900", "--dump-dom", path.as_uri()],
        capture_output=True, text=True, encoding="utf-8", timeout=120).stdout
    found = re.search(r'data-report="([^"]*)"', dom)
    if not found:
        return ["FAIL 測試頁沒有跑完（看 content.js 有沒有丟出例外）"]
    return [line.strip().replace("&quot;", '"') for line in found.group(1).split("|")]


def main() -> int:
    content = (ROOT / "extension" / "content.js").read_text(encoding="utf-8")
    assert "</script" not in content
    page = make_page(content)
    variants = {
        "anchor": page,
        "js-fallback": page.replace("window.chrome = {",
                                    "CSS.supports = () => false;\nwindow.chrome = {", 1),
    }
    failed = 0
    with tempfile.TemporaryDirectory() as folder:
        for name, html in variants.items():
            print(f"== {name}")
            for line in run(html, Path(folder), name):
                print("  ", line)
                failed += line.startswith("FAIL")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
