"""ground_truth.py 的測試（只用標準函式庫，ctest 會執行）。"""

from __future__ import annotations

import unittest

import ground_truth as gt

SAMPLE = """# 註解
== page1.png
配對: en-manga/page1.png

[對白 直 中 356,70,483,285]
えっと
明日は皆で
東京に行くんだ
ルビ: 明日=あした 皆=みんな 東京=とうきょう 行=い
參考: UM... TOMORROW WE'RE ALL GOING TO TOKYO.

[擬聲詞 直 大 900,5,1060,290]
キーン
參考: DING
備註: 斜著畫

== page2.png

[對白 橫 小 0,0,10,10]
本気で戦う
ルビ: 本気=マジ!
"""


class ParseTest(unittest.TestCase):
    def test_parses_pages_blocks_and_fields(self):
        pages = gt.parse(SAMPLE)
        self.assertEqual([p.image for p in pages], ["page1.png", "page2.png"])
        self.assertEqual(pages[0].pair, "en-manga/page1.png")
        first = pages[0].blocks[0]
        self.assertEqual(first.kind, "對白")
        self.assertEqual(first.direction, "vertical")
        self.assertEqual(first.size, "normal")
        self.assertEqual(first.box, (356, 70, 483, 285))
        self.assertEqual(first.lines, ["えっと", "明日は皆で", "東京に行くんだ"])
        self.assertEqual([(r.base, r.reading, r.meaning) for r in first.ruby],
                         [("明日", "あした", False), ("皆", "みんな", False), ("東京", "とうきょう", False),
                          ("行", "い", False)])
        self.assertEqual(first.reference, "UM... TOMORROW WE'RE ALL GOING TO TOKYO.")
        self.assertEqual(pages[0].blocks[1].note, "斜著畫")

    def test_meaningful_ruby_is_marked_with_exclamation(self):
        ruby = gt.parse(SAMPLE)[1].blocks[0].ruby[0]
        self.assertEqual((ruby.base, ruby.reading, ruby.meaning), ("本気", "マジ", True))

    def test_round_trip(self):
        pages = gt.parse(SAMPLE)
        self.assertEqual(gt.parse(gt.dump(pages)), pages)

    def test_hash_is_text_inside_block_and_comment_outside(self):
        pages = gt.parse("== a.png\n# 註解\n[標題 橫 中 0,0,1,1]\n#今日のおすすめ\n")
        self.assertEqual(pages[0].blocks[0].lines, ["#今日のおすすめ"])
        self.assertEqual(gt.parse(gt.dump(pages)), pages)

    def test_excluded_blocks(self):
        block = gt.Block("擬聲詞", "vertical", "large", (0, 0, 1, 1), ["ヒソ"], note="不評測：散落一片")
        self.assertTrue(block.excluded)
        self.assertFalse(gt.parse(SAMPLE)[0].blocks[1].excluded)

    def test_text_joins_lines_by_language(self):
        block = gt.Block("對白", "horizontal", "normal", (0, 0, 1, 1), ["HELLO", "WORLD"])
        self.assertEqual(block.text("en"), "HELLO WORLD")
        self.assertEqual(block.text("ko"), "HELLO WORLD")
        self.assertEqual(block.text("ja"), "HELLOWORLD")

    def test_errors(self):
        bad = {
            "unknown kind": "== a.png\n[台詞 直 中 0,0,1,1]\nx\n",
            "empty box": "== a.png\n[對白 直 中 5,5,5,9]\nx\n",
            "text outside block": "== a.png\nhello\n",
            "ruby not found": "== a.png\n[對白 直 中 0,0,1,1]\n東京\nルビ: 大阪=おおさか\n",
            "ruby out of order": "== a.png\n[對白 直 中 0,0,1,1]\n東京\nルビ: 京=きょう 東=とう\n",
            "no text": "== a.png\n[對白 直 中 0,0,1,1]\nルビ: 東=とう\n",
            "before first page": "[對白 直 中 0,0,1,1]\nx\n",
        }
        for name, text in bad.items():
            with self.subTest(name):
                with self.assertRaises(gt.FormatError):
                    gt.parse(text)


class IsMarkupTest(unittest.TestCase):
    def test_markup_lines(self):
        for line in ("== a.png", "[對白 直 中 0,0,1,1]", "ルビ: 東=とう", "參考: x", "語言: ja", "備註: x"):
            with self.subTest(line):
                self.assertTrue(gt.is_markup(line))
        for line in ("#今日のおすすめ", "配對: x", "[単独] ニュース", "ルビ", "==x"):
            with self.subTest(line):
                self.assertFalse(gt.is_markup(line))


class NormalizeTest(unittest.TestCase):
    def test_width_ellipsis_and_spaces(self):
        self.assertEqual(gt.normalize("ＨＰ１００ ／ ＭＰ"), "HP100/MP")
        self.assertEqual(gt.normalize("待って…"), gt.normalize("待って．．．"))
        self.assertEqual(gt.normalize("ええ・・・"), "ええ...")

    def test_wave_dash(self):
        self.assertEqual(gt.normalize("午後8:15〜"), gt.normalize("午後8:15～"))
        self.assertEqual(gt.normalize("午後8:15〜"), "午後8:15~")

    def test_curly_quotes(self):
        self.assertEqual(gt.normalize("“EVERYBOPDY” IT’S"), gt.normalize("\"EVERYBOPDY\" IT'S"))


if __name__ == "__main__":
    unittest.main()
