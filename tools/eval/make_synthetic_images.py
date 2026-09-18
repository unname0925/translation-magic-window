"""產生 OCR 比對用的合成圖片（內容固定，每次產生的結果都一樣）。

用 Windows 內建的字型（Arial、Yu Gothic、Malgun Gothic 等），所以產生的圖片只放在本機
（預設 build/ocr_eval/images），不放進倉庫。公開的合成測試集（docs/execution-plan.md 5.5）
之後會改用開源字型。

用法：
    tools/eval/.venv/Scripts/python tools/eval/make_synthetic_images.py [--output DIR]
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO_ROOT = Path(__file__).resolve().parents[2]
FONTS = Path("C:/Windows/Fonts")


def font(name: str, size: int, index: int = 0) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(str(FONTS / name), size, index=index)


def text_lines(size, background, lines, fill, text_font, origin=(24, 24), spacing=16):
    image = Image.new("RGB", size, background)
    draw = ImageDraw.Draw(image)
    x, y = origin
    for line in lines:
        draw.text((x, y), line, font=text_font, fill=fill)
        y += text_font.size + spacing
    return image


def en_document():
    return text_lines((720, 260), "white", [
        "The quick brown fox jumps over the lazy dog.",
        "Save your progress before leaving the area.",
        "Press START to continue, or SELECT for options.",
        "Version 1.2.3 - 2026/09/19 (build 4567)",
    ], "black", font("arial.ttf", 26))


def en_game_dialog():
    image = Image.new("RGB", (720, 240), (20, 24, 40))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((16, 16, 704, 224), radius=14, fill=(30, 50, 120), outline="white",
                           width=3)
    f = font("segoeuib.ttf", 28)
    draw.text((40, 36), "Knight: You shouldn't be here.", font=f, fill="white")
    draw.text((40, 86), "The castle gates close at midnight!", font=f, fill=(255, 230, 120))
    draw.text((40, 150), "> Yes, I understand.", font=font("segoeui.ttf", 24), fill="white")
    return image


def en_small():
    return text_lines((640, 150), (245, 240, 230), [
        "Small print: terms and conditions apply.",
        "HP 120/150  MP 45/60  EXP 12,345",
        "Tip: hold Shift to run faster.",
    ], (40, 40, 40), font("times.ttf", 16), spacing=10)


def ja_document():
    return text_lines((720, 250), "white", [
        "吾輩は猫である。名前はまだ無い。",
        "どこで生れたかとんと見当がつかぬ。",
        "東京都渋谷区、2026年9月19日（土）",
        "セーブデータを読み込みますか？",
    ], "black", font("YuGothM.ttc", 28))


def ja_game_dialog():
    image = Image.new("RGB", (720, 220), (0, 0, 0))
    draw = ImageDraw.Draw(image)
    # 上深下淺的漸層背景
    for y in range(220):
        shade = 30 + y // 4
        draw.line([(0, y), (720, y)], fill=(shade, shade // 2, shade + 20))
    draw.rectangle((12, 12, 708, 208), outline=(255, 255, 255), width=2)
    f = font("YuGothB.ttc", 30)
    draw.text((36, 30), "勇者「ここから先は危険だ。」", font=f, fill="white")
    draw.text((36, 86), "準備はいいか？　はい／いいえ", font=f, fill=(255, 220, 160))
    draw.text((36, 150), "所持金：１２，３４５ゴールド", font=font("msgothic.ttc", 26), fill="white")
    return image


def ja_vertical():
    """直排：每個字往下排。裁切後高度 ≥ 寬度 1.5 倍，會走旋轉 90 度的路徑。"""
    image = Image.new("RGB", (300, 420), (250, 248, 240))
    draw = ImageDraw.Draw(image)
    f = font("YuGothM.ttc", 32)
    columns = ["縦書きの文章", "漫画の吹き出し", "日本語テスト"]
    x = 230
    for column in columns:
        y = 24
        for ch in column:
            draw.text((x, y), ch, font=f, fill="black")
            y += 42
        x -= 80
    return image


def ko_document():
    return text_lines((720, 220), "white", [
        "안녕하세요. 만나서 반갑습니다.",
        "저장하시겠습니까? 예 / 아니요",
        "서울특별시 2026년 9월 19일",
    ], "black", font("malgun.ttf", 28))


def rotated_text():
    """稍微旋轉的文字：檢查透視變換裁切。"""
    base = text_lines((640, 200), "white", [
        "Rotated label: fragile items",
        "回転したテキストの行",
    ], (20, 20, 120), font("YuGothM.ttc", 30), origin=(60, 50))
    return base.rotate(6, resample=Image.BICUBIC, expand=False, fillcolor="white")


def lens_capture():
    """透鏡擷取範圍大小（480×270 的 150%）的畫面，有多個不同顏色的文字區塊。"""
    image = Image.new("RGB", (720, 405), (236, 240, 244))
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, 720, 56), fill=(40, 44, 52))
    draw.text((20, 12), "Settings - Display", font=font("segoeuib.ttf", 26), fill="white")
    draw.text((24, 84), "Resolution: 1920 x 1080", font=font("arial.ttf", 22), fill="black")
    draw.text((24, 126), "画面の明るさを調整します", font=font("YuGothM.ttc", 24), fill="black")
    draw.rectangle((24, 180, 340, 240), fill=(0, 120, 212))
    draw.text((44, 194), "Apply changes", font=font("segoeuib.ttf", 24), fill="white")
    draw.rectangle((380, 180, 696, 240), outline=(120, 120, 120), width=2)
    draw.text((400, 194), "キャンセル", font=font("YuGothB.ttc", 24), fill=(60, 60, 60))
    draw.text((24, 280), "Last saved: 2026-09-19 00:42", font=font("consola.ttf", 20),
              fill=(90, 90, 90))
    draw.text((24, 330), "設定已儲存（繁體中文）", font=font("msgothic.ttc", 22),
              fill=(150, 30, 30))
    return image


GENERATORS = {
    "en_document": en_document,
    "en_game_dialog": en_game_dialog,
    "en_small": en_small,
    "ja_document": ja_document,
    "ja_game_dialog": ja_game_dialog,
    "ja_vertical": ja_vertical,
    "ko_document": ko_document,
    "rotated_text": rotated_text,
    "lens_capture": lens_capture,
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", type=Path, default=REPO_ROOT / "build" / "ocr_eval" / "images")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name, generate in GENERATORS.items():
        path = args.output / f"{name}.png"
        generate().save(path)
        print(path)


if __name__ == "__main__":
    main()
