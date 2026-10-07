"""操控一個測試用的 Edge（載入網頁漫畫擴充功能），給「像使用者一樣試用」的測試 agent 用。

每個指令各自連上 Edge 的遠端除錯埠（CDP），Edge 本身在指令之間一直開著：

    python tools/web_extension/edge_driver.py launch                 # 開 Edge（獨立的設定檔，放在螢幕外）
    python tools/web_extension/edge_driver.py open <網址>            # 目前的分頁開網址
    python tools/web_extension/edge_driver.py translate              # 等於按工具列的「翻譯這一頁」
    python tools/web_extension/edge_driver.py shot <檔案.png>        # 截圖（座標＝網頁的 CSS 像素，可以直接拿來點）
    python tools/web_extension/edge_driver.py click <x> <y>
    python tools/web_extension/edge_driver.py drag <x1> <y1> <x2> <y2>
    python tools/web_extension/edge_driver.py scroll <dy> [<x> <y>]  # 滾輪，正數往下
    python tools/web_extension/edge_driver.py key <Escape|Enter|PageDown|ArrowDown|...>
    python tools/web_extension/edge_driver.py type <文字>
    python tools/web_extension/edge_driver.py status                 # 擴充功能回報的這一頁進度
    python tools/web_extension/edge_driver.py eval <JavaScript>      # 在網頁裡執行（查問題用）
    python tools/web_extension/edge_driver.py log [行數]             # 主程式的記錄最後幾行
    python tools/web_extension/edge_driver.py close

- 用獨立的設定檔（暫存資料夾），不碰使用者自己的 Edge；close 只關這個設定檔的程序。
  每次 launch 都從全新的設定檔開始：Edge 154 會把「開發人員模式沒開」時用 --load-extension 載入的
  擴充功能停用（disable_reasons 1<<24），而且停用的狀態記在設定檔裡，第二次開同一個設定檔就載不起來。
- 「讀取所有網站」的權限要使用者在瀏覽器的對話框按允許，程式按不到：所以載入的是擴充功能的副本，
  manifest 裡直接給好這個權限（ID 一樣，因為公鑰一樣）。第一次使用的權限流程要另外手動測。
- 視窗放在螢幕外（--onscreen 放在螢幕上），關掉背景節流，截圖、動畫照常。
"""

from __future__ import annotations

import asyncio
import base64
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

import struct

import websockets

REPO = Path(__file__).resolve().parents[2]
EDGE = Path("C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe")
PORT = 9340
WORK = Path(tempfile.gettempdir()) / "tmw-edge-qa"
PROFILE = WORK / "profile"
EXTENSION = WORK / "extension"
STATE = WORK / "state.json"
EXTENSION_ID = "iohilhlbinnplmnnlipmamjenegjdggj"
APP_LOG = Path(os.environ.get("LOCALAPPDATA", "")) / "TranslationMagicWindow" / "logs" / "translation-magic-window.log"


def targets() -> list[dict]:
    return json.loads(urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json", timeout=5).read())


def page_target() -> dict:
    """目前操作的分頁：記下來的那個；不在了就挑第一個一般網頁。"""
    pages = [t for t in targets() if t["type"] == "page" and not t["url"].startswith(("chrome-extension://", "devtools://", "edge://"))]
    if not pages:
        raise SystemExit("沒有開著的分頁")
    state = json.loads(STATE.read_text(encoding="utf-8")) if STATE.exists() else {}
    for page in pages:
        if page["id"] == state.get("page"):
            return page
    STATE.write_text(json.dumps({"page": pages[0]["id"]}), encoding="utf-8")
    return pages[0]


def find_worker() -> dict | None:
    for target in targets():
        if target["type"] == "service_worker" and EXTENSION_ID in target["url"]:
            return target
    return None


async def wake_worker() -> None:
    """背景程式（MV3 service worker）閒置一陣子會被瀏覽器停掉：開一下擴充功能自己的頁面把它叫醒。"""
    version = json.loads(urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json/version", timeout=5).read())
    async with websockets.connect(version["webSocketDebuggerUrl"], max_size=None) as ws:
        async def call(method, my, **params):
            await ws.send(json.dumps({"id": my, "method": method, "params": params}))
            while True:
                message = json.loads(await ws.recv())
                if message.get("id") == my:
                    return message.get("result", {})
        created = await call("Target.createTarget", 1, url=f"chrome-extension://{EXTENSION_ID}/popup.html",
                             background=True)
        for _ in range(20):
            if find_worker():
                break
            await asyncio.sleep(0.25)
        await call("Target.closeTarget", 2, targetId=created.get("targetId", ""))


def worker_target() -> dict:
    target = find_worker()
    if target is None:
        asyncio.run(wake_worker()) if not asyncio_running() else None
        target = find_worker()
    if target is None:
        raise SystemExit(f"找不到擴充功能的背景程式（{disable_reasons()}）")
    return target


def asyncio_running() -> bool:
    try:
        asyncio.get_running_loop()
        return True
    except RuntimeError:
        return False


class Session:
    def __init__(self, target: dict):
        self.url = target["webSocketDebuggerUrl"]
        self.page = target.get("type") == "page"
        self.counter = 0

    async def __aenter__(self):
        self.ws = await websockets.connect(self.url, max_size=None)
        if self.page:
            # 分頁帶到最前面：在背景的分頁會被當成「隱藏中」（document.hidden），
            # 有些網站隱藏中會蓋上遮罩、停止翻頁動畫
            await self.call("Page.bringToFront")
        return self

    async def __aexit__(self, *exc):
        await self.ws.close()

    async def call(self, method: str, **params):
        self.counter += 1
        my = self.counter
        await self.ws.send(json.dumps({"id": my, "method": method, "params": params}))
        while True:
            message = json.loads(await self.ws.recv())
            if message.get("id") == my:
                if "error" in message:
                    raise SystemExit(f"{method} 失敗：{message['error']}")
                return message.get("result", {})

    async def evaluate(self, expression: str):
        result = await self.call("Runtime.evaluate", expression=expression, awaitPromise=True, returnByValue=True)
        if "exceptionDetails" in result:
            raise SystemExit("執行出錯：" + json.dumps(result["exceptionDetails"], ensure_ascii=False)[:500])
        return result.get("result", {}).get("value")


def disable_reasons() -> str:
    """設定檔裡記的、這個擴充功能被停用的原因（載入失敗時給人看）。"""
    for name in ("Secure Preferences", "Preferences"):
        path = PROFILE / "Default" / name
        try:
            settings = json.loads(path.read_text(encoding="utf-8"))
            entry = settings["extensions"]["settings"][EXTENSION_ID]
            return f"{name} 裡的 disable_reasons：{entry.get('disable_reasons')}"
        except (OSError, KeyError, ValueError):
            continue
    return "設定檔裡沒有這個擴充功能的紀錄"


def launch(onscreen: bool) -> None:
    close(quiet=True)  # 上次的還開著就先關掉，舊的設定檔才刪得掉
    if PROFILE.exists():
        shutil.rmtree(PROFILE, ignore_errors=True)
    WORK.mkdir(parents=True, exist_ok=True)
    if EXTENSION.exists():
        shutil.rmtree(EXTENSION)
    shutil.copytree(REPO / "extension", EXTENSION)
    manifest = json.loads((EXTENSION / "manifest.json").read_text(encoding="utf-8"))
    manifest["host_permissions"] = manifest.pop("optional_host_permissions", ["<all_urls>"])
    (EXTENSION / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    # 預設用無頭模式：沒有實體視窗，螢幕關掉、鎖定時網頁也不會被當成「隱藏中」（document.hidden）。
    # 有些網站（soraraw）隱藏中會蓋上「內容已暫時隱藏」的遮罩，翻譯的截圖也截不到東西
    position = "--window-position=40,40" if onscreen else "--headless=new"
    # 無頭模式的瀏覽器識別字串有「HeadlessEdg」：網站可能給不同的版面或擋掉，換成一般 Edge 的
    version = subprocess.run(["powershell", "-NoProfile", "-Command", f"(Get-Item '{EDGE}').VersionInfo.ProductVersion"],
                             capture_output=True, text=True, check=False).stdout.strip() or "154.0.0.0"
    agent = (f"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
             f"Chrome/{version.split('.')[0]}.0.0.0 Safari/537.36 Edg/{version}")
    subprocess.Popen([str(EDGE), f"--user-data-dir={PROFILE}", f"--remote-debugging-port={PORT}",
                      "--no-first-run", "--no-default-browser-check", "--disable-features=msEdgeFirstRunExperience",
                      "--disable-backgrounding-occluded-windows", "--disable-renderer-backgrounding",
                      # 不登入、不同步：全新的設定檔會被 Edge 自動用 Windows 的帳號登入，
                      # 把使用者帳號裡的擴充功能裝進來、把測試時的瀏覽紀錄同步回帳號
                      "--disable-sync", "--no-service-autorun",
                      "--disable-features=msImplicitSignin,msEdgeSyncConsent,msSignInRequired,EdgeSync",
                      "--window-size=1400,950", position, f"--user-agent={agent}",
                      f"--load-extension={EXTENSION}", "about:blank"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(60):
        try:
            if find_worker() is None:
                raise LookupError
            print(f"Edge 開好了（埠 {PORT}），擴充功能已載入")
            return
        except Exception:
            time.sleep(0.5)
    raise SystemExit(f"Edge 開了，但擴充功能沒有載入（{disable_reasons()}）")


def close(quiet: bool = False) -> None:
    # 只關測試設定檔的 msedge.exe（指令列裡有這個設定檔的路徑），不碰使用者自己的 Edge
    script = ("Get-CimInstance Win32_Process -Filter \"Name='msedge.exe'\" | "
              "Where-Object { $_.CommandLine -like '*tmw-edge-qa*' } | "
              "ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }")
    subprocess.run(["powershell", "-NoProfile", "-Command", script], check=False)
    for _ in range(20):  # 等程序真的結束，設定檔才不會還被鎖著
        check = subprocess.run(["powershell", "-NoProfile", "-Command",
                                "(Get-CimInstance Win32_Process -Filter \"Name='msedge.exe'\" | "
                                "Where-Object { $_.CommandLine -like '*tmw-edge-qa*' }).Count"],
                               capture_output=True, text=True, check=False)
        if check.stdout.strip() in ("", "0"):
            break
        time.sleep(0.5)
    if not quiet:
        print("關了測試用的 Edge（剩下的測試 Edge 程序：0）")


async def screenshot(path: str) -> None:
    async with Session(page_target()) as s:
        size = await s.evaluate("({ w: innerWidth, h: innerHeight, dpr: devicePixelRatio, "
                                "x: visualViewport.pageLeft, y: visualViewport.pageTop })")
        # 截成 CSS 像素的大小（縮放 1／devicePixelRatio）：圖上的座標可以直接拿來 click
        shot = await s.call("Page.captureScreenshot", format="png", clip={
            # clip 是整份網頁的座標：捲動之後要從目前的位置截（以前固定從頂端截，長條模式截到空白）
            "x": size["x"], "y": size["y"], "width": size["w"], "height": size["h"], "scale": 1 / size["dpr"]})
        data = base64.b64decode(shot["data"])
        Path(path).write_bytes(data)
        width, height = struct.unpack(">II", data[16:24])  # PNG 的 IHDR
        print(f"截圖 {path}（{width}×{height}；網頁 {size['w']}×{size['h']}，座標就是網頁座標）")


async def mouse(kind: str, x: float, y: float, **extra) -> None:
    async with Session(page_target()) as s:
        await s.call("Input.dispatchMouseEvent", type=kind, x=x, y=y, **extra)


async def click(x: float, y: float) -> None:
    async with Session(page_target()) as s:
        await s.call("Input.dispatchMouseEvent", type="mouseMoved", x=x, y=y)
        await s.call("Input.dispatchMouseEvent", type="mousePressed", x=x, y=y, button="left", clickCount=1)
        await s.call("Input.dispatchMouseEvent", type="mouseReleased", x=x, y=y, button="left", clickCount=1)


async def drag(x1: float, y1: float, x2: float, y2: float) -> None:
    async with Session(page_target()) as s:
        await s.call("Input.dispatchMouseEvent", type="mouseMoved", x=x1, y=y1)
        await s.call("Input.dispatchMouseEvent", type="mousePressed", x=x1, y=y1, button="left", clickCount=1)
        for step in range(1, 11):
            await s.call("Input.dispatchMouseEvent", type="mouseMoved", x=x1 + (x2 - x1) * step / 10,
                         y=y1 + (y2 - y1) * step / 10, button="left", buttons=1)
            await asyncio.sleep(0.02)
        await s.call("Input.dispatchMouseEvent", type="mouseReleased", x=x2, y=y2, button="left", clickCount=1)


async def scroll(dy: float, x: float, y: float) -> None:
    async with Session(page_target()) as s:
        remaining = dy
        while abs(remaining) > 0:  # 一格一格捲，像真的滾輪
            step = max(-120, min(120, remaining))
            await s.call("Input.dispatchMouseEvent", type="mouseWheel", x=x, y=y, deltaX=0, deltaY=step)
            remaining -= step
            await asyncio.sleep(0.03)


KEYS = {"Escape": 27, "Enter": 13, "PageDown": 34, "PageUp": 33, "ArrowDown": 40, "ArrowUp": 38,
        "ArrowLeft": 37, "ArrowRight": 39, "Space": 32, "Home": 36, "End": 35, "Tab": 9}


async def key(name: str) -> None:
    async with Session(page_target()) as s:
        code = KEYS.get(name)
        if code is None:
            raise SystemExit(f"不認得的按鍵：{name}（可以用 {', '.join(KEYS)}）")
        for kind in ("rawKeyDown", "keyUp"):
            await s.call("Input.dispatchKeyEvent", type=kind, key=name if name != "Space" else " ",
                         windowsVirtualKeyCode=code)


async def type_text(text: str) -> None:
    async with Session(page_target()) as s:
        await s.call("Input.insertText", text=text)


async def open_url(url: str) -> None:
    async with Session(page_target()) as s:
        await s.call("Page.navigate", url=url)
    print(f"開了 {url}")


async def in_worker(expression: str):
    if find_worker() is None:
        await wake_worker()
    async with Session(worker_target()) as s:
        return await s.evaluate(expression)


async def tab_id() -> int:
    page = page_target()
    tabs = await in_worker("chrome.tabs.query({}).then((t) => t.map((x) => ({ id: x.id, url: x.url })))")
    for tab in tabs:
        if tab.get("url") == page["url"]:  # 有些分頁（空白頁之類）查不到網址
            return tab["id"]
    raise SystemExit("找不到這個分頁的 tab id")


async def translate() -> None:
    tab = await tab_id()
    # 背景程式的 start：注入 content.js、開始翻譯（和按控制視窗的「翻譯這一頁」一樣）
    await in_worker(f"(async () => {{ await chrome.tabs.update({tab}, {{ active: true }}); await start({tab}); return true; }})()")
    print("開始翻譯這一頁")


async def status() -> None:
    tab = await tab_id()
    result = await in_worker(f"chrome.tabs.sendMessage({tab}, {{ kind: 'page-status' }}).catch((e) => ({{ error: String(e) }}))")
    print(json.dumps(result, ensure_ascii=False, indent=1))


async def evaluate(expression: str) -> None:
    async with Session(page_target()) as s:
        print(json.dumps(await s.evaluate(expression), ensure_ascii=False, indent=1))


def log(lines: int) -> None:
    text = APP_LOG.read_text(encoding="utf-8", errors="replace").splitlines()
    print("\n".join(text[-lines:]))


def main(argv: list[str]) -> None:
    if not argv:
        print(__doc__)
        return
    command, args = argv[0], argv[1:]
    if command == "launch":
        launch("--onscreen" in args)
    elif command == "close":
        close()
    elif command == "open":
        asyncio.run(open_url(args[0]))
    elif command == "translate":
        asyncio.run(translate())
    elif command == "shot":
        asyncio.run(screenshot(args[0]))
    elif command == "click":
        asyncio.run(click(float(args[0]), float(args[1])))
    elif command == "drag":
        asyncio.run(drag(*map(float, args[:4])))
    elif command == "scroll":
        x, y = (float(args[1]), float(args[2])) if len(args) >= 3 else (700.0, 450.0)
        asyncio.run(scroll(float(args[0]), x, y))
    elif command == "key":
        asyncio.run(key(args[0]))
    elif command == "type":
        asyncio.run(type_text(" ".join(args)))
    elif command == "status":
        asyncio.run(status())
    elif command == "eval":
        asyncio.run(evaluate(" ".join(args)))
    elif command == "log":
        log(int(args[0]) if args else 30)
    else:
        raise SystemExit(f"不認得的指令：{command}")


if __name__ == "__main__":
    main(sys.argv[1:])
