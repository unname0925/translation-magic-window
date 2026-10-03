"""註冊（或取消註冊）網頁漫畫擴充功能的 Native Messaging 主機：tmw_web_host.exe。

Chrome、Edge 只透過登錄機碼找主機（目前使用者，不需要系統管理員權限）：
    HKCU\\Software\\Google\\Chrome\\NativeMessagingHosts\\io.github.unname0925.tmw
    HKCU\\Software\\Microsoft\\Edge\\NativeMessagingHosts\\io.github.unname0925.tmw
值是主機 manifest（JSON）的路徑。manifest 放在 tmw_web_host.exe 旁邊，路徑寫相對的，
整個資料夾搬家只要重新註冊。只有 allowed_origins 列出的擴充功能連得上。

安裝程式會做同樣的事；這個腳本給開發時用（指向建置出來的 exe）。

    python tools/web_extension/register_host.py                 # 建置資料夾的 exe、開發用的擴充功能 ID
    python tools/web_extension/register_host.py --dry-run       # 只印出會做什麼
    python tools/web_extension/register_host.py --unregister
"""

from __future__ import annotations

import argparse
import json
import sys
import winreg
from pathlib import Path

NAME = "io.github.unname0925.tmw"
REPO = Path(__file__).resolve().parents[2]
DEFAULT_EXE = REPO / "build" / "ci" / "bin" / "Release" / "tmw_web_host.exe"
DEV_KEY = REPO / ".cache" / "extension" / "dev-key.txt"  # 第二行是開發用的擴充功能 ID
BROWSERS = {
    "Chrome": rf"Software\Google\Chrome\NativeMessagingHosts\{NAME}",
    "Edge": rf"Software\Microsoft\Edge\NativeMessagingHosts\{NAME}",
}


def manifest(extension_ids: list[str]) -> dict:
    return {
        "name": NAME,
        "description": "Translation Magic Window：網頁漫畫整頁翻譯",
        "path": "tmw_web_host.exe",
        "type": "stdio",
        "allowed_origins": [f"chrome-extension://{ident}/" for ident in extension_ids],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    parser.add_argument("--extension-id", action="append", default=[],
                        help="允許連線的擴充功能 ID（可以給好幾個）。沒給時用開發用的 ID")
    parser.add_argument("--unregister", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    if args.unregister:
        for browser, key in BROWSERS.items():
            print(f"刪除 HKCU\\{key}（{browser}）")
            if not args.dry_run:
                try:
                    winreg.DeleteKey(winreg.HKEY_CURRENT_USER, key)
                except FileNotFoundError:
                    pass
        return 0

    exe = args.exe.resolve()
    if not exe.exists():
        print(f"找不到 {exe}：先建置 tmw_web_host")
        return 1
    ids = args.extension_id
    if not ids:
        if not DEV_KEY.exists():
            print(f"沒有給 --extension-id，也找不到 {DEV_KEY}")
            return 1
        ids = [DEV_KEY.read_text(encoding="utf-8").splitlines()[1].strip()]
    target = exe.parent / f"{NAME}.json"
    content = json.dumps(manifest(ids), ensure_ascii=False, indent=2) + "\n"
    print(f"寫入 {target}：\n{content}")
    if not args.dry_run:
        target.write_text(content, encoding="utf-8")
    for browser, key in BROWSERS.items():
        print(f"HKCU\\{key}（{browser}）= {target}")
        if not args.dry_run:
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER, key) as handle:
                winreg.SetValueEx(handle, "", 0, winreg.REG_SZ, str(target))
    return 0


if __name__ == "__main__":
    sys.exit(main())
