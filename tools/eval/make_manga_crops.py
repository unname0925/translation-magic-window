"""產生 manga-ocr 比對用的合成對話框（內容固定，每次產生的結果都一樣）。

manga-ocr 的輸入是「一個對話框的裁切圖」，不是整頁漫畫。這裡用 Windows 內建的日文字型畫出
直排和橫排的對白，只放在本機（預設 build/manga_ocr/crops），不放進倉庫。

直排是自己一個字一個字排的（Pillow 沒有直排排版功能）：長音、刪節號、括號等轉 90 度，
、。放在格子的右上角。也刻意放了連續重複的字（例如「ドドドドド」），檢查解碼時
「不可重複的三字組」規則是否和官方一致。

另外有漫畫特有的小字和擬聲詞（見 docs/design.md 4.4）：振り仮名（ルビ）、
作者刻意的特殊讀音（本気／マジ）、畫在對話框外的大型擬聲詞（描邊、傾斜、效果線背景）。

用法：
    tools/eval/.venv-manga/Scripts/python tools/eval/make_manga_crops.py [--output DIR]
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO_ROOT = Path(__file__).resolve().parents[2]
FONTS = Path("C:/Windows/Fonts")

# 直排時要轉 90 度的字
ROTATE = set("ー〜～…‥―－（）「」『』【】［］〔〕｛｝(){}[]<>＜＞")
# 直排時放在格子右上角的字
CORNER = set("、。，．")


def font(name: str, size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(str(FONTS / name), size)


def draw_vertical(columns: list[str], text_font, size: int, fill="black", background="white",
                  margin: int = 24, gap: int = 12) -> Image.Image:
    """由右到左的直排文字。"""
    cell = size + 4
    height = margin * 2 + cell * max(len(c) for c in columns)
    width = margin * 2 + len(columns) * cell + (len(columns) - 1) * gap
    image = Image.new("RGB", (width, height), background)
    x = width - margin - cell
    for column in columns:
        y = margin
        for ch in column:
            glyph = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
            draw = ImageDraw.Draw(glyph)
            if ch in CORNER:
                draw.text((cell * 0.45, -cell * 0.35), ch, font=text_font, fill=fill)
            else:
                draw.text((cell / 2, cell / 2), ch, font=text_font, fill=fill, anchor="mm")
            if ch in ROTATE:
                glyph = glyph.rotate(-90)
            image.paste(glyph, (x, y), glyph)
            y += cell
        x -= cell + gap
    return image


def draw_vertical_ruby(columns: list[list[tuple[str, str]]], font_name: str, size: int,
                       margin: int = 24, gap: int = 18) -> Image.Image:
    """有振り仮名（ルビ）的直排：每一欄是 [(本文, ルビ), ...]，ルビ畫在本文右邊的窄欄。"""
    base_font = font(font_name, size)
    ruby_size = max(8, size * 45 // 100)
    ruby_font = font(font_name, ruby_size)
    cell = size + 4
    ruby_cell = ruby_size + 2
    column_width = cell + ruby_cell
    length = max(sum(len(base) for base, _ in column) for column in columns)
    height = margin * 2 + cell * length
    width = margin * 2 + len(columns) * column_width + (len(columns) - 1) * gap
    image = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(image)
    x = width - margin - column_width
    for column in columns:
        y = margin
        for base, ruby in column:
            for i, ch in enumerate(base):
                draw.text((x + cell / 2, y + i * cell + cell / 2), ch, font=base_font, fill="black",
                          anchor="mm")
            if ruby:
                # ルビ沿著本文的長度置中
                span = len(base) * cell
                ruby_y = y + (span - len(ruby) * ruby_cell) / 2
                for i, ch in enumerate(ruby):
                    draw.text((x + cell + ruby_cell / 2, ruby_y + i * ruby_cell + ruby_cell / 2), ch,
                              font=ruby_font, fill="black", anchor="mm")
            y += len(base) * cell
        x -= column_width + gap
    return image


def draw_horizontal_ruby(segments: list[tuple[str, str]], font_name: str, size: int,
                         margin: int = 24) -> Image.Image:
    """有振り仮名的橫排：ルビ畫在本文上方。"""
    base_font = font(font_name, size)
    ruby_size = max(8, size * 45 // 100)
    ruby_font = font(font_name, ruby_size)
    probe = ImageDraw.Draw(Image.new("RGB", (1, 1)))
    width = margin * 2 + int(sum(probe.textlength(base, font=base_font) for base, _ in segments))
    height = margin * 2 + ruby_size + 4 + size
    image = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(image)
    x = margin
    for base, ruby in segments:
        base_width = probe.textlength(base, font=base_font)
        if ruby:
            draw.text((x + base_width / 2, margin + ruby_size / 2), ruby, font=ruby_font,
                      fill="black", anchor="mm")
        draw.text((x, margin + ruby_size + 4), base, font=base_font, fill="black")
        x += base_width
    return image


def sound_effect(text: str, size: int, vertical: bool, angle: float) -> Image.Image:
    """漫畫的擬聲詞：粗體、白字黑色描邊、傾斜，畫在有線條的背景上（不在對話框裡）。"""
    text_font = font("YuGothB.ttc", size)
    stroke = max(2, size // 12)
    cell = size + stroke * 2
    if vertical:
        layer = Image.new("RGBA", (cell + 20, cell * len(text) + 20), (0, 0, 0, 0))
        draw = ImageDraw.Draw(layer)
        for i, ch in enumerate(text):
            glyph = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
            ImageDraw.Draw(glyph).text((cell / 2, cell / 2), ch, font=text_font, fill="white",
                                       anchor="mm", stroke_width=stroke, stroke_fill="black")
            if ch in ROTATE:
                glyph = glyph.rotate(-90)
            layer.paste(glyph, (10, 10 + i * cell), glyph)
    else:
        probe = ImageDraw.Draw(Image.new("RGB", (1, 1)))
        width = int(probe.textlength(text, font=text_font)) + stroke * 2 + 20
        layer = Image.new("RGBA", (width, cell + 20), (0, 0, 0, 0))
        ImageDraw.Draw(layer).text((10 + stroke, 10 + stroke), text, font=text_font, fill="white",
                                   stroke_width=stroke, stroke_fill="black")
    layer = layer.rotate(angle, resample=Image.BICUBIC, expand=True)
    background = Image.new("RGB", (layer.width + 40, layer.height + 40), (235, 235, 235))
    draw = ImageDraw.Draw(background)
    # 背景的集中線，像漫畫的效果線
    cx, cy = background.width / 2, background.height / 2
    for i in range(0, 360, 12):
        r = max(background.width, background.height)
        draw.line([(cx, cy), (cx + r * math.cos(math.radians(i)), cy + r * math.sin(math.radians(i)))],
                  fill=(120, 120, 120), width=2)
    background.paste(layer, (20, 20), layer)
    return background


def draw_horizontal(lines: list[str], text_font, size: int, fill="black", background="white",
                    margin: int = 24, spacing: int = 10) -> Image.Image:
    probe = ImageDraw.Draw(Image.new("RGB", (1, 1)))
    width = max(int(probe.textlength(line, font=text_font)) for line in lines) + margin * 2
    height = margin * 2 + len(lines) * (size + spacing)
    image = Image.new("RGB", (width, height), background)
    draw = ImageDraw.Draw(image)
    for i, line in enumerate(lines):
        draw.text((margin, margin + i * (size + spacing)), line, font=text_font, fill=fill)
    return image


def bubble(image: Image.Image) -> Image.Image:
    """外面加一圈對話框的外框和灰色背景（漫畫的對話框裁切圖常常帶到框線）。"""
    pad = 18
    out = Image.new("RGB", (image.width + pad * 2, image.height + pad * 2), (150, 150, 150))
    draw = ImageDraw.Draw(out)
    draw.rounded_rectangle((4, 4, out.width - 5, out.height - 5), radius=40, fill="white",
                           outline="black", width=4)
    out.paste(image, (pad, pad))
    return out


def screentone(image: Image.Image, spacing: int = 6) -> Image.Image:
    """在白色部分加上網點（漫畫常見的背景）。"""
    out = image.copy()
    pixels = out.load()
    for y in range(0, out.height, spacing):
        for x in range((y // spacing % 2) * spacing // 2, out.width, spacing):
            if pixels[x, y] == (255, 255, 255):
                for dx in range(2):
                    for dy in range(2):
                        if x + dx < out.width and y + dy < out.height:
                            pixels[x + dx, y + dy] = (170, 170, 170)
    return out


def crops() -> dict[str, Image.Image]:
    goth_m, goth_b, msgoth = "YuGothM.ttc", "YuGothB.ttc", "msgothic.ttc"
    return {
        "v_two_columns": bubble(draw_vertical(["「そんなこと、", "ありえない！」"],
                                              font(goth_b, 34), 34)),
        "v_three_columns": bubble(draw_vertical(["明日の朝までに", "この手紙を", "届けてほしい"],
                                                font(goth_m, 32), 32)),
        "v_long_vowel": bubble(draw_vertical(["ちょっと待って…", "まだ終わってないよー"],
                                             font(goth_m, 30), 30)),
        "v_katakana": bubble(draw_vertical(["キャラクターの", "ショックが", "ヤバい"],
                                           font(msgoth, 30), 30)),
        "v_repeat_sfx": draw_vertical(["ドドドドドド"], font(goth_b, 44), 44, fill="white",
                                      background="black"),
        "v_repeat_vowel": bubble(draw_vertical(["ああああああ", "いやだあああ"], font(goth_m, 30), 30)),
        "v_screentone": screentone(bubble(draw_vertical(["夢じゃない", "本当なんだ"],
                                                        font(goth_b, 36), 36))),
        "v_long_text": bubble(draw_vertical(
            ["昔々ある村に", "とても優しい", "おじいさんと", "おばあさんが", "住んでいました。"],
            font(goth_m, 26), 26)),
        "v_small": bubble(draw_vertical(["小さな文字も", "読めるかな？"], font(goth_m, 16), 16,
                                        margin=10, gap=6)),
        "h_one_line": bubble(draw_horizontal(["ありがとう！"], font(goth_b, 36), 36)),
        "h_two_lines": bubble(draw_horizontal(["今日はいい天気ですね。", "散歩に行こうか？"],
                                              font(goth_m, 30), 30)),
        "h_ascii_digits": bubble(draw_horizontal(["HP 100 / MP 50", "Level 5 クリア!!"],
                                                 font(goth_m, 30), 30)),
        "h_white_on_black": draw_horizontal(["――その日、世界は変わった。"], font(goth_m, 30), 30,
                                            fill="white", background="black"),
        # 振り仮名（ルビ）：本文照常辨識，ルビ是額外的資訊
        "ruby_vertical": bubble(draw_vertical_ruby(
            [[("東京", "とうきょう"), ("に行く", "")], [("明日", "あした"), ("の", ""), ("予定", "よてい")]],
            goth_m, 34)),
        "ruby_horizontal": bubble(draw_horizontal_ruby(
            [("東京", "とうきょう"), ("タワーに", ""), ("登", "のぼ"), ("った", "")], goth_m, 34)),
        # 作者刻意的特殊讀音：本氣 → マジ
        "ruby_special_reading": bubble(draw_vertical_ruby(
            [[("本気", "マジ"), ("で", "")], [("戦", "たたか"), ("うぞ", "")]], goth_b, 36)),
        # 擬聲詞／擬態詞：對話框外、描邊、傾斜
        "sfx_dokidoki": sound_effect("ドキドキ", 64, vertical=False, angle=8),
        "sfx_gaan": sound_effect("ガーン", 72, vertical=True, angle=-6),
        "sfx_zaazaa": sound_effect("ザアザアザア", 52, vertical=False, angle=0),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", type=Path,
                        default=REPO_ROOT / "build" / "manga_ocr" / "crops")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name, image in crops().items():
        path = args.output / f"{name}.png"
        image.save(path)
        print(path)


if __name__ == "__main__":
    main()
