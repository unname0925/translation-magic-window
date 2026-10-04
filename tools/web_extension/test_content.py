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
      if (message.kind !== "translate") return {{ type: "engines", engines: [], current: "" }};
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
          // 下一個畫格之前（不等延遲掃描）譯文就要在：捲動時不會一瞬間露出原文
          setTimeout(() => check("元素重建後、下一個畫格之前就蓋上譯文",
                                 fresh.nextElementSibling?.tagName === "TMW-OVERLAY"), 0);
          setTimeout(() => {{
            check("元素重建後譯文立刻回來、不重新翻譯",
                  fresh.nextElementSibling?.tagName === "TMW-OVERLAY" && requests.length === before,
                  "requests " + before + " → " + requests.length);
            check("重建後仍對齊", misalignment() < 1, "worst " + misalignment().toFixed(2) + "px");
            // 網頁的程式把我們的元素拿掉（框架重繪）
            overlays()[0].remove();
            setTimeout(() => {{
              check("被網頁拿掉的譯文會補回去", overlays().length === 12, overlays().length + " overlays");
              // 閱讀器捲動時一直把目前的頁碼寫進網址（MangaDex）：那不是換章，譯文不能被拆掉
              const removedOverlays = [];
              const watch = new MutationObserver((ms) => ms.forEach((m) => m.removedNodes.forEach((n) => {{
                if (n.tagName === "TMW-OVERLAY") removedOverlays.push(n);
              }})));
              watch.observe(document.body, {{ childList: true, subtree: true }});
              for (let page = 21; page < 26; page++) history.replaceState(null, "", "#page-" + page);
              setTimeout(() => {{
              watch.disconnect();
              check("網址的頁碼一直變（不是換章）時譯文不會被拆掉", removedOverlays.length === 0 && overlays().length === 12,
                    removedOverlays.length + " removed, " + overlays().length + " overlays");
              let reply = null;
              listener({{ kind: "page-status" }}, {{}}, (r) => reply = r);
              check("控制視窗：狀態", reply && reply.active && reply.total === 12 && reply.done === 12 && reply.failed === 0, JSON.stringify(reply));
              listener({{ kind: "toggle" }}, {{}}, (r) => reply = r);
              check("控制視窗：切換原文", reply.visible === false);
              listener({{ kind: "stop" }}, {{}}, (r) => reply = r);
              check("控制視窗：停止後譯文和狀態列都拿掉", overlays().length === 0 && !document.querySelector("tmw-panel") && !window.__tmwWebManga,
                    overlays().length + " overlays");
              check("停止後不留 anchor-name", ![...document.images].some((i) => i.style.getPropertyValue("anchor-name")));
              document.body.setAttribute("data-report", report.join(" | "));
              document.title = "DONE";
              }}, 1200);  // 換章偵測每 0.5 秒看一次網址：等它跑過再檢查
            }}, 1500);
          }}, 600);
        }}, 1200);
      }}, 600);
    }}, 2500);
    </script>"""
    return page


def make_loader_page(content: str) -> str:
    """網址不放在屬性裡、只有捲到畫面附近才由網頁自己的程式設定的圖：要靠「載入整頁」自動捲動。"""
    page = f"""<!doctype html><meta charset=utf-8>
    <style>
      body {{ margin: 0; }}
      #reader {{ width: 640px; margin: 0 auto; }}
      #reader img {{ display: block; width: 600px; height: 900px; margin: 10px auto; background: #eee; }}
    </style>
    <div id=reader></div>
    <script>
    // 用 canvas 做 600×900 的圖當「漫畫頁」（data: 網址，不必連網）
    const canvas = document.createElement("canvas");
    canvas.width = 600; canvas.height = 900;
    const reader = document.getElementById("reader");
    const urls = [];
    for (let i = 0; i < 10; i++) {{
      const c = canvas.getContext("2d");
      c.fillStyle = "#" + ((i + 1) * 1234567 % 0xffffff).toString(16).padStart(6, "0");
      c.fillRect(0, 0, 40 + i * 10, 40);
      urls.push(canvas.toDataURL());
      reader.append(document.createElement("img"));
    }}
    // 網頁自己的延遲載入：每 100 ms 看哪些圖到了畫面附近才給網址（網址只在這個程式裡）
    setInterval(() => {{
      [...reader.querySelectorAll("img")].forEach((img, i) => {{
        if (!img.getAttribute("src") && img.getBoundingClientRect().top < innerHeight * 1.2) img.src = urls[i];
      }});
    }}, 100);
    let counter = 0;
    const requests = [];
    let listener = null;
    window.chrome = {{ runtime: {{ onMessage: {{ addListener: (f) => listener = f, removeListener: () => listener = null }},
                                  sendMessage: async (message) => {{
      if (message.kind !== "translate") return {{ type: "engines", engines: [], current: "" }};
      const id = message.url || ("inline-" + (counter++));
      requests.push(id);
      await new Promise((r) => setTimeout(r, 30));
      return {{ type: "result", id, width: 600, height: 900, error: "", patchesDropped: 0,
               items: [{{ rect: [100, 100, 300, 400], text: "譯文", vertical: true,
                          foreground: "#000000", background: "#ffffff", size: "normal", lineThickness: 30, ruby: [] }}] }};
    }} }} }};
    </script>
    <script>{content}</script>
    <script>
    const report = [];
    function check(name, ok, detail = "") {{ report.push((ok ? "PASS " : "FAIL ") + name + (detail ? " (" + detail + ")" : "")); }}
    setTimeout(() => {{
      check("載入整頁：只有捲到才給網址的圖，自動捲動後全部翻到", requests.length === 10, requests.length + " requests");
      check("載入整頁之後捲回原本的位置", Math.abs(scrollY) < 1, "scrollY " + scrollY);
      let reply = null;
      listener({{ kind: "page-status" }}, {{}}, (r) => reply = r);
      check("載入整頁結束、全部翻完", reply && reply.preloading === false && reply.done === 10, JSON.stringify(reply));
      check("控制面板在頁面上", Boolean(document.querySelector("tmw-panel")));
      document.body.setAttribute("data-report", report.join(" | "));
    }}, 20000);
    </script>"""
    return page


def make_failure_page(content: str) -> str:
    """第一次翻譯被限流、重試成功；閱讀器把一半的圖拿掉之後總數不能變少。"""
    PIXEL = "data:image/gif;base64,R0lGODlhAQABAAAAACH5BAEKAAEALAAAAAABAAEAAAICTAEAOw=="
    page = f"""<!doctype html><meta charset=utf-8>
    <style>
      body {{ margin: 0; }}
      #reader img {{ display: block; width: 600px; height: 900px; margin: 10px auto; }}
    </style>
    <div id=reader></div>
    <script>
    const reader = document.getElementById("reader");
    for (let i = 0; i < 8; i++) {{
      const img = document.createElement("img");
      img.src = "{PIXEL}";
      img.dataset.src = "https://cdn.example.invalid/f" + i + ".jpg";
      reader.append(img);
    }}
    const attempts = {{}};
    let listener = null;
    window.chrome = {{ runtime: {{ onMessage: {{ addListener: (f) => listener = f, removeListener: () => listener = null }},
                                  sendMessage: async (message) => {{
      if (message.kind !== "translate") return {{ type: "engines", engines: [], current: "" }};
      attempts[message.url] = (attempts[message.url] || 0) + 1;
      await new Promise((r) => setTimeout(r, 20));
      const items = [{{ rect: [100, 100, 300, 400], text: "譯文", vertical: true, foreground: "#000000",
                        background: "#ffffff", size: "normal", lineThickness: 30, ruby: [] }}];
      // 第一次：主程式回「結果」但翻譯引擎失敗（以前會被當成成功）
      const error = attempts[message.url] === 1 ? "被限流或額度用完：HTTP 429" : "";
      return {{ type: "result", id: message.url, width: 600, height: 900, error, notice: "",
               patchesDropped: 0, items: error ? [] : items }};
    }} }} }};
    </script>
    <script>{content}</script>
    <script>
    const report = [];
    function check(name, ok, detail = "") {{ report.push((ok ? "PASS " : "FAIL ") + name + (detail ? " (" + detail + ")" : "")); }}
    const status = () => {{ let r = null; listener({{ kind: "page-status" }}, {{}}, (x) => r = x); return r; }};
    setTimeout(() => {{
      let s = status();
      check("翻譯引擎失敗算失敗、不算完成", s.failed === 8 && s.done === 0, JSON.stringify(s));
      check("列出失敗的原因", s.reasons.length === 1 && s.reasons[0].includes("HTTP 429") && s.reasons[0].includes("8 張"),
            JSON.stringify(s.reasons));
      listener({{ kind: "retry" }}, {{}}, () => {{}});
      setTimeout(() => {{
        s = status();
        check("重試真的重送、這次成功", s.done === 8 && s.failed === 0 && Object.values(attempts).every((n) => n === 2),
              JSON.stringify(s) + " " + JSON.stringify(Object.values(attempts)));
        // 閱讀器把離開畫面的一半拿掉
        [...reader.querySelectorAll("img")].slice(4).forEach((img) => {{ img.nextElementSibling?.remove(); img.remove(); }});
        setTimeout(() => {{
          s = status();
          check("圖片元素被拿掉，總數不變少", s.total === 8 && s.done === 8, JSON.stringify(s));
          document.body.setAttribute("data-report", report.join(" | "));
        }}, 2500);
      }}, 3000);
    }}, 4000);
    </script>"""
    return page


def make_blocked_page(content: str) -> str:
    """圖片伺服器有防盜連、背景抓不到：要算成「等圖片載入」、說明原因，並叫網頁直接載入。"""
    PIXEL = "data:image/gif;base64,R0lGODlhAQABAAAAACH5BAEKAAEALAAAAAABAAEAAAICTAEAOw=="
    page = f"""<!doctype html><meta charset=utf-8>
    <style>
      body {{ margin: 0; }}
      #reader img {{ display: block; width: 600px; height: 900px; margin: 10px auto; }}
    </style>
    <div id=reader></div>
    <script>
    const reader = document.getElementById("reader");
    for (let i = 0; i < 5; i++) {{
      const img = document.createElement("img");
      img.src = "{PIXEL}";
      img.dataset.src = "https://cdn.example.invalid/b" + i + ".jpg";
      reader.append(img);
    }}
    const pagesSent = [];
    let listener = null;
    window.chrome = {{ runtime: {{ onMessage: {{ addListener: (f) => listener = f, removeListener: () => listener = null }},
                                  sendMessage: async (message) => {{
      if (message.kind !== "translate") return {{ type: "engines", engines: [], current: "" }};
      pagesSent.push(message.page);
      return {{ type: "error", fetchFailed: true, message: "HTTP 403" }};
    }} }} }};
    </script>
    <script>{content}</script>
    <script>
    const report = [];
    function check(name, ok, detail = "") {{ report.push((ok ? "PASS " : "FAIL ") + name + (detail ? " (" + detail + ")" : "")); }}
    setTimeout(() => {{
      let s = null;
      listener({{ kind: "page-status" }}, {{}}, (x) => s = x);
      check("背景抓不到的圖算成等圖片載入，不算失敗", s.waiting === 5 && s.failed === 0, JSON.stringify(s));
      check("說明原因", s.reasons.some((r) => r.includes("HTTP 403") && r.includes("5 張")), JSON.stringify(s.reasons));
      check("抓圖時附上這一頁的網址（防盜連）", pagesSent.length > 0 && pagesSent.every((p) => p === location.href));
      check("叫網頁直接載入（網址搬到 src）",
            [...reader.querySelectorAll("img")].every((img) => img.getAttribute("src") === img.dataset.src));
      document.body.setAttribute("data-report", report.join(" | "));
    }}, 3000);
    </script>"""
    return page


def make_blob_page(content: str) -> str:
    """像 MangaDex：圖是 blob: 網址，捲動時閱讀器換成內容相同的新 blob: 網址。譯文不能消失、也不能重翻。"""
    page = f"""<!doctype html><meta charset=utf-8>
    <style>
      body {{ margin: 0; }}
      #reader img {{ display: block; width: 600px; height: 900px; margin: 10px auto; }}
    </style>
    <div id=reader></div>
    <script>
    const reader = document.getElementById("reader");
    const blobs = [];
    const makeBlob = (i) => new Promise((resolve) => {{
      const canvas = document.createElement("canvas");
      canvas.width = 600; canvas.height = 900;
      const c = canvas.getContext("2d");
      c.fillStyle = "#fff"; c.fillRect(0, 0, 600, 900);
      c.fillStyle = "#" + ((i + 3) * 2345671 % 0xffffff).toString(16).padStart(6, "0");
      c.fillRect(50 + i * 20, 80, 200, 300);
      canvas.toBlob(resolve, "image/png");
    }});
    let requests = 0;
    let listener = null;
    window.chrome = {{ runtime: {{ onMessage: {{ addListener: (f) => listener = f, removeListener: () => listener = null }},
                                  sendMessage: async (message) => {{
      if (message.kind !== "translate") return {{ type: "engines", engines: [], current: "" }};
      requests++;
      await new Promise((r) => setTimeout(r, 20));
      return {{ type: "result", id: "x" + requests, width: 600, height: 900, error: "", notice: "", patchesDropped: 0,
               items: [{{ rect: [100, 100, 300, 400], text: "譯文", vertical: true, foreground: "#000000",
                          background: "#ffffff", size: "normal", lineThickness: 30, ruby: [] }}] }};
    }} }} }};
    </script>
    <script>
    (async () => {{
      for (let i = 0; i < 6; i++) {{
        const blob = await makeBlob(i);
        blobs.push(blob);
        const img = document.createElement("img");
        img.src = URL.createObjectURL(blob);
        reader.append(img);
      }}
      await new Promise((r) => setTimeout(r, 200));
      const script = document.createElement("script");
      script.textContent = document.getElementById("content").textContent;
      document.body.append(script);
    }})();
    </script>
    <script type="text/plain" id="content">{content}</script>
    <script>
    const report = [];
    function check(name, ok, detail = "") {{ report.push((ok ? "PASS " : "FAIL ") + name + (detail ? " (" + detail + ")" : "")); }}
    const overlays = () => document.querySelectorAll("tmw-overlay").length;
    // 6 張 blob: 圖產生完、content.js 開始執行之後，等翻好（最多 20 秒）
    const ready = () => new Promise((resolve) => {{
      const started = Date.now();
      const poll = () => (window.__tmwWebManga && overlays() === 6) || Date.now() - started > 20000
        ? resolve() : setTimeout(poll, 100);
      poll();
    }});
    ready().then(() => {{
      check("blob: 網址的圖都翻好", overlays() === 6 && requests === 6, overlays() + " overlays, " + requests + " requests");
      const before = requests;
      // 閱讀器換成內容相同的新 blob: 網址
      [...reader.querySelectorAll("img")].forEach((img, i) => {{
        URL.revokeObjectURL(img.src);
        img.src = URL.createObjectURL(blobs[i]);
      }});
      setTimeout(() => check("換網址的當下譯文還在", overlays() === 6, overlays() + " overlays"), 0);
      setTimeout(() => {{
        check("換成新的 blob: 網址後譯文還在、不重翻", overlays() === 6 && requests === before,
              overlays() + " overlays, requests " + before + " → " + requests);
        document.body.setAttribute("data-report", report.join(" | "));
      }}, 1500);
    }});
    </script>"""
    return page


def run(page: str, folder: Path, name: str) -> list[str]:
    path = folder / f"{name}.html"
    path.write_text(page, encoding="utf-8")
    dom = subprocess.run(
        [str(CHROME), "--headless=new", "--disable-gpu", "--no-first-run",
         f"--user-data-dir={folder / 'profile'}", "--virtual-time-budget=40000",
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
        "page-loader": make_loader_page(content),
        "failures": make_failure_page(content),
        "blocked": make_blocked_page(content),
        "blob-swap": make_blob_page(content),
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
