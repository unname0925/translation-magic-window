"""產生 THIRD_PARTY_NOTICES.txt：程式裡用到的每個元件、模型、字型和資料的授權（M5-06）。

授權文字一律從實際的檔案讀（vcpkg 安裝的 copyright、NuGet 套件裡的 LICENSE），不憑記憶寫。
本機找不到全文的（Qt、字型、辭典）列出授權名稱和全文的網址，發布前要把全文一起放進安裝程式。

    python tools/notices/make_notices.py --build build/ci --output THIRD_PARTY_NOTICES.txt

換了相依套件的版本（vcpkg.json 的 baseline、cmake/OnnxRuntime.cmake、cmake/Qt.cmake）之後重跑一次。
"""

from __future__ import annotations

import argparse
import json
import sys
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
TRIPLET = "x64-windows-static-md"

# 靜態連結進執行檔的 vcpkg 套件（x64-windows 底下的是建置用的工具，不會發布；
# tclap 只給 OpenCC 的命令列工具用，沒有連結進來）
VCPKG_PACKAGES = [
    ("cpr", "HTTP 用戶端（翻譯引擎）"),
    ("curl", "HTTP（cpr 使用）"),
    ("zlib", "壓縮（curl 使用）"),
    ("opencc", "簡體轉台灣繁體"),
    ("marisa-trie", "OpenCC 的字典結構"),
    ("darts-clone", "OpenCC 的字典結構（只有標頭檔）"),
    ("rapidjson", "OpenCC 讀設定（只有標頭檔）"),
    ("opencv4", "OCR 的影像處理"),
    ("yaml-cpp", "讀 OCR 模型的設定"),
    ("nlohmann-json", "JSON（只有標頭檔）"),
    ("spdlog", "記錄檔"),
    ("fmt", "字串格式（spdlog 使用）"),
]

# 隨程式散布的 DLL 所在的 NuGet 套件：（顯示名稱, 壓縮檔, 授權檔, 第三方聲明檔）
NUGET_PACKAGES = [
    ("ONNX Runtime 1.24.4（onnxruntime.dll）", "microsoft.ml.onnxruntime.directml.1.24.4.zip",
     "LICENSE", "ThirdPartyNotices.txt"),
    ("DirectML 1.15.4（DirectML.dll；預設不附、用 Windows 內建的，以 -DTMW_BUNDLE_DIRECTML=ON 建置時才附上）", "microsoft.ai.directml.1.15.4.zip", "LICENSE.txt",
     "ThirdPartyNotices.txt"),
]

# 本機沒有授權全文的元件和資料
OTHERS = [
    ("Qt 6.9.3（qtbase：Qt6Core、Qt6Gui、Qt6Widgets 和平台外掛）", "LGPL-3.0-only",
     "動態連結，可以換成自己建置的 Qt。原始碼：https://download.qt.io/archive/qt/6.9/6.9.3/",
     "https://www.gnu.org/licenses/lgpl-3.0.txt"),
    ("JMdict、KANJIDIC2（models/furigana/readings.tsv 由它們產生）", "CC BY-SA 4.0",
     "Electronic Dictionary Research and Development Group，https://www.edrdg.org/",
     "https://creativecommons.org/licenses/by-sa/4.0/legalcode"),
    ("Noto Sans TC、Noto Serif TC（models/fonts）", "SIL Open Font License 1.1",
     "Google，https://github.com/google/fonts", "https://openfontlicense.org/open-font-license-official-text/"),
    ("jf open 粉圓（models/fonts）", "SIL Open Font License 1.1",
     "justfont，https://github.com/justfont/open-huninn-font",
     "https://openfontlicense.org/open-font-license-official-text/"),
]

RULE = "=" * 78


def section(title: str, body: str) -> str:
    return f"{RULE}\n{title}\n{RULE}\n\n{body.strip()}\n\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build", type=Path, default=REPO_ROOT / "build" / "ci",
                        help="建置資料夾（讀 vcpkg_installed）")
    parser.add_argument("--downloads", type=Path, default=REPO_ROOT / ".cache" / "downloads",
                        help="NuGet 套件的下載資料夾")
    parser.add_argument("--output", type=Path, default=REPO_ROOT / "THIRD_PARTY_NOTICES.txt")
    args = parser.parse_args(argv)

    share = args.build / "vcpkg_installed" / TRIPLET / "share"
    missing: list[str] = []
    out = [
        "Translation Magic Window 使用的第三方元件、模型、字型和資料\n"
        "Third-party components, models, fonts and data used by Translation Magic Window\n\n"
        "本程式以 GPL-3.0 授權（見 LICENSE）。以下各項依各自的授權使用。\n"
        "此檔案由 tools/notices/make_notices.py 產生，請不要手動修改。\n\n",
    ]

    out.append(section("目錄", "\n".join(
        [f"- {name}：{purpose}" for name, purpose in VCPKG_PACKAGES]
        + [f"- {name}" for name, *_ in NUGET_PACKAGES]
        + [f"- {name}（{license_}）" for name, license_, *_ in OTHERS]
        + ["- OCR 和漫畫的模型（見最後一節）"])))

    for name, purpose in VCPKG_PACKAGES:
        path = share / name / "copyright"
        if not path.is_file():
            missing.append(str(path))
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        out.append(section(f"{name}（{purpose}）", text))

    for name, archive, license_file, notices_file in NUGET_PACKAGES:
        path = args.downloads / archive
        if not path.is_file():
            missing.append(str(path))
            continue
        with zipfile.ZipFile(path) as package:
            license_text = package.read(license_file).decode("utf-8-sig", errors="replace")
            notices = package.read(notices_file).decode("utf-8-sig", errors="replace")
        out.append(section(name, license_text + "\n\n----- " + notices_file + " -----\n\n" + notices))

    for name, license_, origin, url in OTHERS:
        out.append(section(f"{name}", f"授權：{license_}\n來源：{origin}\n授權全文：{url}"))

    manifest = json.loads((REPO_ROOT / "tools" / "fetch_models" / "models.json")
                          .read_text(encoding="utf-8"))
    lines = []
    for model in manifest["models"]:
        lines.append(f"- {model['id']}：{model['purpose']}\n  授權：{model['license']}\n"
                     f"  來源：{model['source']}")
    out.append(section("模型（由 tools/fetch_models 從原始來源下載，不包含在原始碼倉庫裡）",
                       "\n".join(lines)))

    if missing:
        for path in missing:
            print(f"找不到 {path}（先建置一次 ci preset，或重新下載 NuGet 套件）", file=sys.stderr)
        return 1
    args.output.write_text("".join(out), encoding="utf-8", newline="\n")
    print(f"{args.output}：{sum(len(part) for part in out) / 1024:.0f} KB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
