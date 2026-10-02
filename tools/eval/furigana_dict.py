"""ルビ的讀音是「一般讀音」還是「作者刻意的特殊讀音」（M2-13，design.md 4.4、4.5）。

判斷方式：
1. 讀音能用每個漢字的讀音（KANJIDIC2 的音讀、訓讀、名乗り）依序拼出來 → 一般讀音。
   拼的時候考慮連濁（か→が、は→ば／ぱ）、促音（字尾的つ／く／ち／き→っ）、「々」重複前一個字。
2. 拼不出來，但 JMdict 記載這個詞就是這樣唸（今日＝きょう、大人＝おとな、倫敦＝ロンドン）→ 一般讀音。
3. 其餘 → 特殊讀音（本気＝マジ、強敵＝とも、地球＝ほし）。
不認得的漢字（辭典沒有）無法判斷，回傳 None，呼叫端退回「讀音是片假名」的舊規則。

產品不需要整本辭典：`build` 只輸出每個漢字的讀音，以及 JMdict 裡「拼不出來」的正規讀法（第 2 點）。

    tools/eval/.venv/Scripts/python tools/eval/furigana_dict.py build \\
        --jmdict .cache/dict/JMdict_e.gz --kanjidic .cache/dict/kanjidic2.xml.gz \\
        --output models/furigana/readings.tsv

辭典的授權：JMdict、KANJIDIC2 是 EDRDG 的 CC BY-SA 4.0，輸出的檔案是衍生作品，要註明出處
（檔案開頭有註解）。
"""

from __future__ import annotations

import argparse
import gzip
import sys
import xml.etree.ElementTree as ET
from functools import lru_cache
from pathlib import Path

VOICED = {
    "か": "が", "き": "ぎ", "く": "ぐ", "け": "げ", "こ": "ご",
    "さ": "ざ", "し": "じ", "す": "ず", "せ": "ぜ", "そ": "ぞ",
    "た": "だ", "ち": "ぢ", "つ": "づ", "て": "で", "と": "ど",
    "は": "ば", "ひ": "び", "ふ": "ぶ", "へ": "べ", "ほ": "ぼ",
}
HALF_VOICED = {"は": "ぱ", "ひ": "ぴ", "ふ": "ぷ", "へ": "ぺ", "ほ": "ぽ"}


def to_hiragana(text: str) -> str:
    return "".join(chr(ord(c) - 0x60) if "ァ" <= c <= "ヶ" else c for c in text)


def is_kanji(c: str) -> bool:
    return "一" <= c <= "鿿" or "㐀" <= c <= "䶿" or c == "々"


def variants(reading: str) -> set[str]:
    """一個漢字的讀音，加上連濁和促音的變化。"""
    out = {reading}
    first = reading[:1]
    for table in (VOICED, HALF_VOICED):
        if first in table:
            out.add(table[first] + reading[1:])
    for word in list(out):
        if len(word) >= 2 and word[-1] in "つくちき":
            out.add(word[:-1] + "っ")
    return out


def load_kanjidic(path: Path) -> dict[str, set[str]]:
    readings: dict[str, set[str]] = {}
    with gzip.open(path, "rb") as file:
        for _, element in ET.iterparse(file):
            if element.tag != "character":
                continue
            literal = element.findtext("literal")
            found: set[str] = set()
            for r in element.iter("reading"):
                kind = r.get("r_type")
                text = (r.text or "").strip()
                if kind == "ja_on":
                    found.add(to_hiragana(text).strip("-"))
                elif kind == "ja_kun":
                    text = text.strip("-")
                    stem, _, okurigana = text.partition(".")
                    found.add(stem)
                    found.add(stem + okurigana)
            for n in element.iter("nanori"):
                found.add(to_hiragana((n.text or "").strip()))
            found.discard("")
            if literal:
                readings[literal] = found
            element.clear()
    return readings


def derivable(base: str, reading: str, kanji: dict[str, set[str]]) -> bool | None:
    """reading 能不能用 base 每個字的讀音依序拼出來。有不認得的漢字時回傳 None。"""
    reading = to_hiragana(reading)
    for c in base:
        if is_kanji(c) and c != "々" and c not in kanji:
            return None

    @lru_cache(maxsize=None)
    def match(i: int, j: int, previous: str) -> bool:
        if i == len(base):
            return j == len(reading)
        c = base[i]
        if not is_kanji(c):
            # 夾在中間的假名照原樣比對（片假名也轉成平假名）
            k = to_hiragana(c)
            return reading.startswith(k, j) and match(i + 1, j + len(k), "")
        options = kanji.get(previous if c == "々" else c, set())
        for option in options:
            for form in variants(option):
                if form and reading.startswith(form, j) and match(i + 1, j + len(form), c if c != "々" else previous):
                    return True
        return False

    return match(0, 0, "")


def strip_shared_kana(word: str, reading: str) -> tuple[str, str] | None:
    """去掉詞頭、詞尾和讀音一樣的假名，剩下的漢字部分和它的讀音。沒有可以去的就回傳 None。"""
    start = 0
    while start < len(word) and not is_kanji(word[start]) and reading[start:start + 1] == to_hiragana(word[start]):
        start += 1
    end_word, end_reading = len(word), len(reading)
    while end_word > start and not is_kanji(word[end_word - 1]) and end_reading > start and             reading[end_reading - 1] == to_hiragana(word[end_word - 1]):
        end_word -= 1
        end_reading -= 1
    if (start, end_word) == (0, len(word)) or start >= end_word or start >= end_reading:
        return None
    return word[start:end_word], reading[start:end_reading]


def load_irregular(path: Path, kanji: dict[str, set[str]]) -> set[tuple[str, str]]:
    """JMdict 裡用每個字的讀音拼不出來的正規讀法（熟字訓、外來語的漢字寫法…）。"""
    irregular: set[tuple[str, str]] = set()
    with gzip.open(path, "rb") as file:
        for _, element in ET.iterparse(file):
            if element.tag != "entry":
                continue
            kebs = [k.findtext("keb") for k in element.findall("k_ele")]
            for r in element.findall("r_ele"):
                reb = to_hiragana(r.findtext("reb") or "")
                restrictions = [x.text for x in r.findall("re_restr")]
                if r.find("re_nokanji") is not None:
                    continue
                for keb in kebs:
                    if not keb or (restrictions and keb not in restrictions):
                        continue
                    if derivable(keb, reb, kanji) is not True:
                        irregular.add((keb, reb))
                        # ルビ 常常只標在漢字上：「お母さん」的「母」標「かあ」。
                        # 把詞頭、詞尾和讀音相同的假名剝掉，也收進去
                        stripped = strip_shared_kana(keb, reb)
                        if stripped:
                            irregular.add(stripped)
            element.clear()
    return irregular


def classify(base: str, reading: str, kanji: dict[str, set[str]],
             irregular: set[tuple[str, str]]) -> bool | None:
    """True 是特殊讀音，False 是一般讀音，None 是無法判斷。"""
    if (base, to_hiragana(reading)) in irregular:
        return False
    result = derivable(base, reading, kanji)
    if result is None:
        return None
    return not result


def build(args) -> int:
    kanji = load_kanjidic(Path(args.kanjidic))
    irregular = load_irregular(Path(args.jmdict), kanji)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="\n") as file:
        file.write("# ルビ的一般讀音（tools/eval/furigana_dict.py build 產生，M2-13）\n")
        file.write("# 資料來源：JMdict、KANJIDIC2，(C) Electronic Dictionary Research and Development Group，\n")
        file.write("# CC BY-SA 4.0（https://www.edrdg.org/edrdg/licence.html）。這個檔案是衍生作品，授權相同。\n")
        file.write("# 「K 字\t讀音,讀音」是每個漢字的讀音；「W 詞\t讀音」是拼不出來的正規讀法。\n")
        for c in sorted(kanji):
            file.write(f"K {c}\t{','.join(sorted(kanji[c]))}\n")
        for word, reading in sorted(irregular):
            file.write(f"W {word}\t{reading}\n")
    print(f"{len(kanji)} 個漢字、{len(irregular)} 個拼不出來的正規讀法 → {output}"
          f"（{output.stat().st_size / 1e6:.1f} MB）")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    b = sub.add_parser("build")
    b.add_argument("--jmdict", required=True)
    b.add_argument("--kanjidic", required=True)
    b.add_argument("--output", required=True)
    args = parser.parse_args()
    return build(args)


if __name__ == "__main__":
    sys.exit(main())
