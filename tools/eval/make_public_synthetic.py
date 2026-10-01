"""產生公開的合成測試集（docs/execution-plan.md 5.5、M2-12）：圖片和正確答案都放進倉庫。

只用開源字型（Noto Sans JP、Noto Sans KR，SIL Open Font License），所以畫出來的圖片可以公開。
字型檔不放進倉庫（約 20 MB），放在 .cache/fonts：

    NotoSansJP-wght.ttf  https://github.com/google/fonts/raw/main/ofl/notosansjp/NotoSansJP%5Bwght%5D.ttf
    NotoSansKR-wght.ttf  https://github.com/google/fonts/raw/main/ofl/notosanskr/NotoSansKR%5Bwght%5D.ttf

    tools/eval/.venv/Scripts/python tools/eval/make_public_synthetic.py

輸出 testdata/synthetic/<情境>.png 和 testdata/synthetic/expected.json（每張圖的語言和每一行的文字）。
內容固定，每次產生的圖片都一樣；改了情境之後要重新產生，並用 check_synthetic.py --update 更新基準線。
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO_ROOT = Path(__file__).resolve().parents[2]
FONTS = REPO_ROOT / ".cache" / "fonts"
OUTPUT = REPO_ROOT / "testdata" / "synthetic"


def font(language: str, size: int, weight: int = 400) -> ImageFont.FreeTypeFont:
    """language 是 ja（日文和英文都用它）或 ko。weight 是可變字型的粗細（400 一般、700 粗體）。"""
    name = "NotoSansKR-wght.ttf" if language == "ko" else "NotoSansJP-wght.ttf"
    loaded = ImageFont.truetype(str(FONTS / name), size)
    loaded.set_variation_by_axes([weight])
    return loaded


def text_lines(size, background, lines, fill, text_font, origin=(24, 24), spacing=16):
    image = Image.new("RGB", size, background)
    draw = ImageDraw.Draw(image)
    x, y = origin
    for line in lines:
        draw.text((x, y), line, font=text_font, fill=fill)
        y += text_font.size + spacing
    return image


def en_document():
    lines = ["The quick brown fox jumps over the lazy dog.",
             "Save your progress before leaving the area.",
             "Press START to continue, or SELECT for options.",
             "Version 1.2.3 - 2026/09/19 (build 4567)"]
    return text_lines((720, 260), "white", lines, "black", font("ja", 26)), "en", lines


def en_game_dialog():
    image = Image.new("RGB", (720, 240), (20, 24, 40))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((16, 16, 704, 224), radius=14, fill=(30, 50, 120), outline="white",
                           width=3)
    bold = font("ja", 28, 700)
    lines = ["Knight: You shouldn't be here.", "The castle gates close at midnight!",
             "> Yes, I understand."]
    draw.text((40, 36), lines[0], font=bold, fill="white")
    draw.text((40, 86), lines[1], font=bold, fill=(255, 230, 120))
    draw.text((40, 150), lines[2], font=font("ja", 24), fill="white")
    return image, "en", lines


def en_small():
    lines = ["Small print: terms and conditions apply.", "HP 120/150  MP 45/60  EXP 12,345",
             "Tip: hold Shift to run faster."]
    return (text_lines((640, 150), (245, 240, 230), lines, (40, 40, 40), font("ja", 16),
                       spacing=10), "en", lines)


def ja_document():
    lines = ["吾輩は猫である。名前はまだ無い。", "どこで生れたかとんと見当がつかぬ。",
             "東京都渋谷区、2026年9月19日（土）", "セーブデータを読み込みますか？"]
    return text_lines((720, 250), "white", lines, "black", font("ja", 28)), "ja", lines


def ja_game_dialog():
    image = Image.new("RGB", (720, 220), (0, 0, 0))
    draw = ImageDraw.Draw(image)
    for y in range(220):  # 上深下淺的漸層背景
        shade = 30 + y // 4
        draw.line([(0, y), (720, y)], fill=(shade, shade // 2, shade + 20))
    draw.rectangle((12, 12, 708, 208), outline=(255, 255, 255), width=2)
    bold = font("ja", 30, 700)
    lines = ["勇者「ここから先は危険だ。」", "準備はいいか？　はい／いいえ", "所持金：１２，３４５ゴールド"]
    draw.text((36, 30), lines[0], font=bold, fill="white")
    draw.text((36, 86), lines[1], font=bold, fill=(255, 220, 160))
    draw.text((36, 150), lines[2], font=font("ja", 26), fill="white")
    return image, "ja", lines


def vertical_columns(columns, size, background, text_font, start_x, step_x, fill="black"):
    """直排：每個字往下排，欄由右到左。裁切後高度 ≥ 寬度 1.5 倍，會走旋轉 90 度的路徑。"""
    image = Image.new("RGB", size, background)
    draw = ImageDraw.Draw(image)
    x = start_x
    for column in columns:
        y = 24
        for ch in column:
            draw.text((x, y), ch, font=text_font, fill=fill)
            y += text_font.size + 10
        x -= step_x
    return image


def ja_vertical():
    columns = ["縦書きの文章", "漫画の吹き出し", "日本語テスト"]
    return (vertical_columns(columns, (300, 420), (250, 248, 240), font("ja", 32), 230, 80),
            "ja", columns)


def ja_manga_bubble():
    """灰色網點背景上的白色對話框，裡面兩欄粗體直排（漫畫最常見的樣子）。"""
    image = Image.new("RGB", (360, 460), (200, 200, 200))
    draw = ImageDraw.Draw(image)
    for y in range(0, 460, 6):  # 網點
        for x in range((y // 6) % 2 * 3, 360, 6):
            draw.ellipse((x, y, x + 2, y + 2), fill=(150, 150, 150))
    draw.ellipse((40, 20, 320, 440), fill="white", outline="black", width=3)
    bold = font("ja", 34, 700)
    columns = ["本気で言ってる", "のか！？"]
    x = 200
    for column in columns:
        y = 80
        for ch in column:
            draw.text((x, y), ch, font=bold, fill="black")
            y += 44
        x -= 60
    return image, "ja", columns


def ko_document():
    lines = ["안녕하세요. 만나서 반갑습니다.", "저장하시겠습니까? 예 / 아니요",
             "서울특별시 2026년 9월 19일"]
    return text_lines((720, 220), "white", lines, "black", font("ko", 28)), "ko", lines


def ko_webtoon():
    """韓文條漫：粉色背景上的圓角對話框，橫排粗體。"""
    image = Image.new("RGB", (480, 300), (250, 220, 230))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((30, 40, 450, 260), radius=40, fill="white", outline=(60, 60, 60),
                           width=3)
    bold = font("ko", 30, 700)
    lines = ["진짜 그렇게", "생각하는 거야?"]
    draw.text((90, 100), lines[0], font=bold, fill="black")
    draw.text((90, 160), lines[1], font=bold, fill="black")
    return image, "ko", lines


def rotated_text():
    """稍微旋轉的文字：檢查透視變換裁切。"""
    lines = ["Rotated label: fragile items", "回転したテキストの行"]
    base = text_lines((640, 200), "white", lines, (20, 20, 120), font("ja", 30), origin=(60, 50))
    return base.rotate(6, resample=Image.BICUBIC, expand=False, fillcolor="white"), "ja", lines


def lens_capture():
    """透鏡擷取範圍大小（480×270 的 150%）的畫面，有多個不同顏色的文字區塊。"""
    image = Image.new("RGB", (720, 405), (236, 240, 244))
    draw = ImageDraw.Draw(image)
    lines = ["Settings - Display", "Resolution: 1920 x 1080", "画面の明るさを調整します",
             "Apply changes", "キャンセル", "Last saved: 2026-09-19 00:42"]
    draw.rectangle((0, 0, 720, 56), fill=(40, 44, 52))
    draw.text((20, 12), lines[0], font=font("ja", 26, 700), fill="white")
    draw.text((24, 84), lines[1], font=font("ja", 22), fill="black")
    draw.text((24, 126), lines[2], font=font("ja", 24), fill="black")
    draw.rectangle((24, 180, 340, 240), fill=(0, 120, 212))
    draw.text((44, 194), lines[3], font=font("ja", 24, 700), fill="white")
    draw.rectangle((380, 180, 696, 240), outline=(120, 120, 120), width=2)
    draw.text((400, 194), lines[4], font=font("ja", 24, 700), fill=(60, 60, 60))
    draw.text((24, 280), lines[5], font=font("ja", 20), fill=(90, 90, 90))
    return image, "ja", lines


# 以下是「劣化」的情境：真實截圖不會像上面那麼乾淨。乾淨的圖對細微的退步不敏感
# （M2-12 實測：辨識的縮放改成最近鄰插值，上面 11 張的結果完全沒變）。

def ja_small_dense():
    """13 像素的小字，行距很密（網頁、遊戲的說明文字）。"""
    lines = ["この先は立入禁止です。許可証を持っている者のみ通行できます。",
             "Items marked with * cannot be sold or traded.",
             "攻撃力＋１２　防御力－３　素早さ＋５",
             "最終更新日：2026年9月30日"]
    return (text_lines((560, 120), (252, 252, 250), lines, (30, 30, 30), font("ja", 13),
                       origin=(10, 10), spacing=6), "ja", lines)


def scaled_blur():
    """在高解析度畫好再縮小到 0.55 倍（模擬 Windows 縮放和瀏覽器縮放後的截圖），字邊緣是糊的。"""
    lines = ["装備を変更しますか？", "Your inventory is full.", "宿屋に泊まる（５０ゴールド）"]
    large = text_lines((1000, 300), (235, 230, 220), lines, (50, 40, 30), font("ja", 44),
                       origin=(40, 30), spacing=30)
    return large.resize((550, 165), Image.BILINEAR), "ja", lines


def jpeg_artifacts():
    """壓縮率很高的 JPEG（網路上的漫畫和截圖常見），存回 PNG 才不會每次產生都不一樣。"""
    import io

    lines = ["Don't let them escape!", "逃がすな！絶対に追いかけろ！", "The bridge is collapsing!"]
    image = text_lines((620, 200), (255, 250, 240), lines, "black", font("ja", 28, 700))
    buffer = io.BytesIO()
    image.save(buffer, format="JPEG", quality=25)
    buffer.seek(0)
    return Image.open(buffer).convert("RGB"), "ja", lines


def low_contrast():
    """淺灰底上的灰字（被選取的選單、停用的按鈕、半透明的字幕）。"""
    lines = ["オプション設定", "Sound volume: 80%", "字幕を表示する"]
    return (text_lines((480, 170), (200, 200, 205), lines, (130, 130, 140), font("ja", 26)),
            "ja", lines)


def noisy_background():
    """有雜訊的背景（遊戲畫面上的字幕）。雜訊用固定的種子，每次產生都一樣。"""
    import random

    generator = random.Random(20260930)
    image = Image.new("RGB", (640, 180))
    pixels = image.load()
    for y in range(180):
        for x in range(640):
            base = 60 + (x + y) % 40
            noise = generator.randint(-25, 25)
            pixels[x, y] = (base + noise, base // 2 + noise, base + 30 + noise)
    lines = ["ここで待っていてくれ。", "I'll be back before sunrise."]
    draw = ImageDraw.Draw(image)
    draw.text((30, 30), lines[0], font=font("ja", 34, 700), fill="white", stroke_width=2,
              stroke_fill="black")
    draw.text((30, 100), lines[1], font=font("ja", 30, 700), fill="white", stroke_width=2,
              stroke_fill="black")
    return image, "ja", lines


def ko_small_blur():
    """韓文的小字加上縮小造成的模糊。"""
    lines = ["다음 화에 계속됩니다", "무단 전재 및 재배포 금지", "작가의 말: 감사합니다!"]
    large = text_lines((800, 260), "white", lines, (40, 40, 40), font("ko", 36),
                       origin=(30, 24), spacing=28)
    return large.resize((400, 130), Image.BILINEAR), "ko", lines


GENERATORS = [en_document, en_game_dialog, en_small, ja_document, ja_game_dialog, ja_vertical,
              ja_manga_bubble, ko_document, ko_webtoon, rotated_text, lens_capture,
              ja_small_dense, scaled_blur, jpeg_artifacts, low_contrast, noisy_background,
              ko_small_blur]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    expected = {}
    for generate in GENERATORS:
        image, language, lines = generate()
        name = f"{generate.__name__}.png"
        image.save(args.output / name, optimize=True)
        expected[name] = {"language": language, "lines": lines}
        print(args.output / name)
    (args.output / "expected.json").write_text(
        json.dumps(expected, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
