"""量 furigana_dict.py 的「一般讀音／特殊讀音」判斷準不準（M2-13）。

兩組資料：
1. 私有正確答案裡的ルビ（testdata/private/ja-*/ground_truth.txt，特殊讀音在讀音後面標 !）。
2. 下面整理的常見例子：漫畫裡常見的特殊讀音（義訓、外來語念法），以及容易被誤判的一般讀音
   （熟字訓、連濁、促音、「々」、只標漢字部分的送假名）。這些是一般的日文知識，不是從截圖來的。

    tools/eval/.venv/Scripts/python tools/eval/evaluate_furigana.py \\
        --jmdict .cache/dict/JMdict_e.gz --kanjidic .cache/dict/kanjidic2.xml.gz
"""

from __future__ import annotations

import argparse
import collections
import sys
from pathlib import Path

import ground_truth as gt
from furigana_dict import classify, load_irregular, load_kanjidic

REPO_ROOT = Path(__file__).resolve().parents[2]

# 作者刻意的讀音（漫畫、輕小說常見的寫法）
SPECIAL = [
    ("本気", "マジ"), ("強敵", "とも"), ("地球", "ほし"), ("宇宙", "そら"), ("運命", "さだめ"),
    ("未来", "あした"), ("仲間", "とも"), ("真剣", "マジ"), ("冗談", "ジョーク"), ("相棒", "パートナー"),
    ("魔術", "マジック"), ("好敵手", "ライバル"), ("能力", "ちから"), ("時間", "とき"), ("人間", "ひと"),
    ("少女", "おとめ"), ("記憶", "おもいで"), ("約束", "ちかい"), ("家族", "ファミリー"),
    ("世界", "ここ"), ("戦場", "ここ"), ("貴様", "おまえ"), ("怪物", "モンスター"), ("聖剣", "エクスカリバー"),
    ("楓男", "フーダン"), ("楓女", "フージョ"), ("環境", "フローラ"),
]
# 一般讀音（容易被誤判的）
ORDINARY = [
    ("今日", "きょう"), ("大人", "おとな"), ("明日", "あした"), ("東京", "とうきょう"), ("学校", "がっこう"),
    ("人々", "ひとびと"), ("一人", "ひとり"), ("戦", "たたか"), ("行", "い"), ("先輩", "せんぱい"),
    ("魔法少女", "まほうしょうじょ"), ("図書館", "としょかん"), ("三日月", "みかづき"), ("時計", "とけい"),
    ("日本", "にほん"), ("竹刀", "しない"), ("雪崩", "なだれ"), ("紅葉", "もみじ"), ("七夕", "たなばた"),
    ("眼鏡", "めがね"), ("倫敦", "ロンドン"), ("珈琲", "コーヒー"), ("手紙", "てがみ"), ("鉄砲", "てっぽう"),
    ("一緒", "いっしょ"), ("発表", "はっぴょう"), ("楓林", "ふうりん"), ("我々", "われわれ"), ("時々", "ときどき"),
    ("山田", "やまだ"), ("佐藤", "さとう"), ("田中", "たなか"), ("鈴木", "すずき"), ("昨日", "きのう"),
    ("下手", "へた"), ("上手", "じょうず"), ("部屋", "へや"), ("二十歳", "はたち"), ("迷子", "まいご"),
    ("本気", "ほんき"), ("地球", "ちきゅう"), ("宇宙", "うちゅう"), ("見", "み"), ("聞", "き"),
    ("故郷", "ふるさと"), ("母", "かあ"), ("父", "とう"), ("兄", "にい"), ("姉", "ねえ"),
]


def evaluate(cases, kanji, irregular, label):
    counts = collections.Counter()
    wrong = []
    for base, reading, special in cases:
        guess = classify(base, reading, kanji, irregular)
        if guess is None:
            counts["無法判斷"] += 1
            wrong.append((base, reading, special, None))
        elif guess == special:
            counts["對"] += 1
        else:
            counts["錯"] += 1
            wrong.append((base, reading, special, guess))
    special_total = sum(1 for c in cases if c[2])
    caught = sum(1 for base, reading, special in cases
                 if special and classify(base, reading, kanji, irregular) is True)
    print(f"== {label}：{len(cases)} 個（特殊 {special_total}）  {dict(counts)}  "
          f"特殊讀音抓到 {caught}/{special_total}")
    for base, reading, special, guess in wrong:
        expected = "特殊" if special else "一般"
        got = "無法判斷" if guess is None else ("特殊" if guess else "一般")
        print(f"   {base}={reading}：應該是{expected}，判成{got}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--jmdict", required=True)
    parser.add_argument("--kanjidic", required=True)
    args = parser.parse_args()
    kanji = load_kanjidic(Path(args.kanjidic))
    irregular = load_irregular(Path(args.jmdict), kanji)

    private = []
    for path in sorted((REPO_ROOT / "testdata" / "private").glob("ja-*/ground_truth.txt")):
        for page in gt.load(path):
            for block in page.blocks:
                for ruby in block.ruby:
                    private.append((ruby.base, ruby.reading, ruby.meaning))
    if private:
        evaluate(private, kanji, irregular, "私有正確答案")
    evaluate([(b, r, True) for b, r in SPECIAL] + [(b, r, False) for b, r in ORDINARY],
             kanji, irregular, "整理的例子")
    return 0


if __name__ == "__main__":
    sys.exit(main())
