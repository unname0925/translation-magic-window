"""產生安裝程式（M5-01）：把要裝的檔案整理到暫存資料夾，再用 Inno Setup 編譯。

    python tools/installer/make_installer.py

需要：
- 先用 ci preset 建置 Release（cmake --build build/ci --config Release）
- models/ 下載好模型（tools/fetch_models），背景修補要先執行 tools/eval/lama_for_directml.py
- Inno Setup 6 的 ISCC.exe（預設找 .cache/innosetup，或用 --iscc 指定）

產生 build/installer/TranslationMagicWindow-<版本>-setup.exe。
"""

from __future__ import annotations

import argparse
import glob
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

# 主程式執行時需要的檔案（相對於建置的 bin/Release）
PROGRAM_FILES = [
    "TranslationMagicWindow.exe",
    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "platforms/qwindows.dll",
    "onnxruntime.dll", "onnxruntime_providers_shared.dll",
]
# Visual C++ 執行階段（用 dumpbin /dependents 查過主程式、Qt、ONNX Runtime 實際用到的）。
# 照 Microsoft 的說明可以和程式放在一起（app-local），使用者不必另外安裝 VC++ 可轉散發套件
CRT_FILES = [
    "concrt140.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll", "vcruntime140.dll", "vcruntime140_1.dll",
]
# 元件 → models 底下的資料夾或檔案。"main" 是一定會裝的：預設會用到的 OCR 模型
# （有顯示卡用 medium、沒有用 small，韓文另外一個）和ルビ讀音表
MODELS = {
    "main": ["PP-OCRv6_medium_det", "PP-OCRv6_medium_rec", "PP-OCRv6_small_det",
             "PP-OCRv6_small_rec", "korean_PP-OCRv5_mobile_rec", "furigana"],
    "manga": ["comic-text-detector", "manga-ocr"],
    "inpaint": ["lama/lama_fp32_dml.onnx"],
    "fonts": ["fonts"],
}
DOCS = {"LICENSE": "LICENSE.txt", "THIRD_PARTY_NOTICES.txt": "THIRD_PARTY_NOTICES.txt",
        "docs/user-guide.md": "docs/user-guide.md", "docs/privacy.md": "docs/privacy.md"}


def project_version() -> str:
    text = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(TranslationMagicWindow\s+VERSION\s+([\d.]+)", text)
    if not match:
        raise SystemExit("CMakeLists.txt 裡找不到版本號")
    return match.group(1)


def find_crt_folder() -> Path:
    # Build Tools 或 Visual Studio 的 VC\Redist\MSVC\<版本>\x64\Microsoft.VC*.CRT，取最新的
    pattern = "C:/Program Files*/Microsoft Visual Studio/*/*/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT"
    found = sorted(Path(p) for p in glob.glob(pattern))
    if not found:
        raise SystemExit("找不到 Visual C++ 執行階段（VC\\Redist\\MSVC\\...\\Microsoft.VC*.CRT）")
    return found[-1]


def isl_keys(text: str) -> set[str]:
    keys = set()
    section = ""
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("["):
            section = line
        elif line and not line.startswith(";") and "=" in line:
            keys.add(f"{section}{line.split('=', 1)[0]}")
    return keys


def copy(source: Path, target: Path, missing: list[str]) -> None:
    if not source.exists():
        missing.append(str(source))
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        shutil.copytree(source, target, dirs_exist_ok=True)
    else:
        shutil.copy2(source, target)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--bin", type=Path, default=REPO_ROOT / "build" / "ci" / "bin" / "Release")
    parser.add_argument("--models", type=Path, default=REPO_ROOT / "models")
    parser.add_argument("--output", type=Path, default=REPO_ROOT / "build" / "installer")
    parser.add_argument("--iscc", type=Path, default=REPO_ROOT / ".cache" / "innosetup" / "ISCC.exe")
    parser.add_argument("--stage-only", action="store_true", help="只整理檔案，不編譯")
    args = parser.parse_args(argv)

    stage = args.output / "stage"
    if stage.exists():
        shutil.rmtree(stage)
    missing: list[str] = []

    main_dir = stage / "main"
    for name in PROGRAM_FILES:
        copy(args.bin / name, main_dir / name, missing)
    copy(args.bin / "opencc", main_dir / "opencc", missing)
    crt = find_crt_folder()
    for name in CRT_FILES:
        copy(crt / name, main_dir / name, missing)
    for source, target in DOCS.items():
        copy(REPO_ROOT / source, main_dir / target, missing)
    for component, entries in MODELS.items():
        for entry in entries:
            copy(args.models / entry, stage / component / "models" / entry, missing)

    # 安裝程式的繁體中文訊息：Default.isl 的每一則都要有，少了會顯示英文
    translation = (HERE / "ChineseTraditional.isl").read_text(encoding="utf-8")
    english = args.iscc.parent / "Default.isl"
    if english.exists():
        untranslated = isl_keys(english.read_text(encoding="utf-8")) - isl_keys(translation)
        if untranslated:
            missing.extend(f"ChineseTraditional.isl 少了 {key}" for key in sorted(untranslated))
    # Inno Setup 要有 BOM 才會當成 UTF-8 讀
    (stage / "ChineseTraditional.isl").write_text(translation, encoding="utf-8-sig")

    if missing:
        for item in missing:
            print(f"缺少：{item}", file=sys.stderr)
        return 1
    if args.stage_only:
        print(f"已整理到 {stage}")
        return 0
    if not args.iscc.exists():
        print(f"找不到 {args.iscc}（Inno Setup 6）", file=sys.stderr)
        return 1
    version = project_version()
    command = [str(args.iscc), "/Q", f"/DStage={stage}", f"/DAppVersion={version}",
               f"/DOutputDir={args.output}", str(HERE / "translation-magic-window.iss")]
    result = subprocess.run(command)
    if result.returncode != 0:
        return result.returncode
    setup = args.output / f"TranslationMagicWindow-{version}-setup.exe"
    print(f"{setup}：{setup.stat().st_size / 2**20:.0f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
