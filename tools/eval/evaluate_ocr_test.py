"""evaluate_ocr.py 的測試（只用標準函式庫，ctest 會執行）。"""

from __future__ import annotations

import unittest

import evaluate_ocr as ev
import ground_truth as gt


def line(text: str, x0: int, y0: int, x1: int, y1: int, score: float = 0.99) -> dict:
    return {"text": text, "score": score, "box": [[x0, y0], [x1, y0], [x1, y1], [x0, y1]]}


def block(box, lines, direction="horizontal", note="", language="", kind="對白") -> gt.Block:
    return gt.Block(kind, direction, "normal", box, list(lines), note=note, language=language)


class LevenshteinTest(unittest.TestCase):
    def test_distance(self):
        self.assertEqual(ev.levenshtein("", ""), 0)
        self.assertEqual(ev.levenshtein("abc", ""), 3)
        self.assertEqual(ev.levenshtein("kitten", "sitting"), 3)
        self.assertEqual(ev.levenshtein("ドドドドドド", "ドドドＦドド"), 1)


class AssignLinesTest(unittest.TestCase):
    def test_each_line_goes_to_the_block_it_overlaps_most(self):
        blocks = [block((0, 0, 100, 100), ["a"]), block((100, 0, 200, 100), ["b"])]
        items = [line("A", 10, 10, 90, 30), line("B", 80, 40, 180, 60),  # 大部分在第二個
                 line("C", 300, 0, 320, 20), line("  ", 10, 10, 20, 20),  # 框外、空白
                 line("D", 60, 60, 140, 80)]  # 兩邊各一半：沒有超過一半，不算
        assigned, unassigned = ev.assign_lines(blocks, items)
        self.assertEqual({i: [x["text"] for x in v] for i, v in assigned.items()},
                         {0: ["A"], 1: ["B"]})
        self.assertEqual([x["text"] for x in unassigned], ["C", "D"])


class HypothesisTest(unittest.TestCase):
    def test_japanese_drops_ruby_and_joins_columns_right_to_left(self):
        b = block((0, 0, 200, 100), ["東京大阪"], direction="vertical")
        items = [line("大阪", 40, 0, 70, 60), line("東京", 100, 0, 130, 60),
                 line("とうきょう", 131, 2, 142, 58)]
        self.assertEqual(ev.block_hypothesis(b, items, "ja"), "東京大阪")

    def test_korean_joins_words_with_space(self):
        b = block((0, 0, 300, 30), ["천둥 치는 소리"])
        items = [line("소리", 130, 0, 180, 20), line("천둥", 0, 0, 50, 20), line("치는", 60, 0, 110, 20)]
        self.assertEqual(ev.block_hypothesis(b, items, "ko"), "천둥 치는 소리")
        two_rows = [line("둘째 줄", 0, 30, 80, 50), line("첫 줄", 0, 0, 60, 20)]
        self.assertEqual(ev.block_hypothesis(b, two_rows, "ko"), "첫 줄 둘째 줄")


class EvaluateTest(unittest.TestCase):
    def setUp(self):
        self.pages = [gt.Page("a.png", blocks=[
            block((0, 0, 100, 20), ["HELLO WORLD"]),
            block((0, 30, 100, 50), ["MISSED"]),
            block((0, 60, 100, 80), ["WATERMARK"], note="不評測：浮水印"),
            block((0, 90, 100, 110), ["ドン"], language="ja", kind="擬聲詞"),
        ])]
        self.ocr = {"images": [{"image": "a.png", "timings_ms": {"detection": 5, "recognition": 7},
                                "lines": [line("HELLO", 0, 0, 45, 20), line("WORLO", 50, 0, 100, 20),
                                          line("WATERMARK", 0, 60, 100, 80),
                                          line("ドン", 0, 90, 40, 110),
                                          line("NOISE", 300, 300, 350, 320)]}]}

    def test_records_and_summary(self):
        result = ev.evaluate("en-manga", self.pages, self.ocr)
        self.assertEqual([(r.index, r.reference, r.hypothesis, r.detected) for r in result.records],
                         [(1, "HELLOWORLD", "HELLOWORLO", True), (2, "MISSED", "", False),
                          (4, "ドン", "ドン", True)])
        self.assertEqual(result.extra_chars, 5)  # NOISE；浮水印的行不算多出來
        self.assertEqual(result.timings, [12])
        summary = ev.Summary.of(ev.main_records(result))  # 日文的擬聲詞另外統計
        self.assertEqual((summary.blocks, summary.characters), (2, 16))
        self.assertAlmostEqual(summary.cer, 7 / 16)
        self.assertEqual((summary.exact, summary.missed), (0, 0.5))
        self.assertIsNone(ev.Summary.of([]))


class KoreanStrategyTest(unittest.TestCase):
    def test_threshold_and_higher_score(self):
        main = {"images": [{"image": "a.png", "lines": [line("HELLO", 0, 0, 10, 10, 0.95),
                                                         line("xx", 0, 20, 10, 30, 0.40),
                                                         line("yy", 0, 40, 10, 50, 0.85),
                                                         line("zz", 0, 60, 10, 70, 0.50)]}]}
        korean = {"images": [{"image": "a.png", "lines": [line("HELL0", 0, 0, 10, 10, 0.97),
                                                           line("안녕", 0, 20, 10, 30, 0.90),
                                                           line("좋아", 0, 40, 10, 50, 0.95),
                                                           line("없음", 0, 60, 10, 70, 0.30)]}]}

        def texts(raw):
            return [x["text"] for x in raw["images"][0]["lines"]]

        self.assertEqual(texts(ev.combine_korean(main, korean, ev.threshold_strategy(0.8))),
                         ["HELLO", "안녕", "yy", "zz"])  # zz：韓文模型的分數更低
        self.assertEqual(texts(ev.combine_korean(main, korean, ev.threshold_strategy(0.9))),
                         ["HELLO", "안녕", "좋아", "zz"])
        self.assertEqual(texts(ev.combine_korean(main, korean, ev.higher_score)),
                         ["HELL0", "안녕", "좋아", "zz"])

    def test_vote_by_image(self):
        main = {"images": [{"image": "a.png", "lines": [line("x", 0, 0, 10, 10, 0.9), line("y", 0, 20, 10, 30, 0.9),
                                                         line("z", 0, 40, 10, 50, 0.9)]},
                           {"image": "b.png", "lines": [line("p", 0, 0, 10, 10, 0.9), line("q", 0, 20, 10, 30, 0.9)]}]}
        korean = {"images": [{"image": "a.png", "lines": [line("가", 0, 0, 10, 10, 0.95), line("나", 0, 20, 10, 30, 0.95),
                                                           line("다", 0, 40, 10, 50, 0.5)]},
                             {"image": "b.png", "lines": [line("라", 0, 0, 10, 10, 0.95), line("마", 0, 20, 10, 30, 0.5)]}]}
        combined = ev.vote_by_image(main, korean)
        self.assertEqual([[x["text"] for x in image["lines"]] for image in combined["images"]],
                         [["가", "나", "다"], ["p", "q"]])  # b.png 剛好一半：不換

    def test_script_rules(self):
        self.assertTrue(ev.hangul_and_higher(line("abc", 0, 0, 1, 1, 0.5), line("가a", 0, 0, 1, 1, 0.6)))
        self.assertFalse(ev.hangul_and_higher(line("abc", 0, 0, 1, 1, 0.5), line("abc", 0, 0, 1, 1, 0.6)))
        self.assertFalse(ev.hangul_and_higher(line("abc", 0, 0, 1, 1, 0.7), line("가", 0, 0, 1, 1, 0.6)))
        # 英文畫面：韓文模型只有一行亂讀出韓文字母，主模型的行沒有假名或漢字 → 1 比 0，整張換成韓文模型；
        # 日文畫面：兩行假名投給主模型 → 1 比 2，不換
        main = {"images": [{"image": "en.png", "lines": [line("Shift", 0, 0, 10, 10, 0.9), line("Tab", 0, 20, 10, 30, 0.5)]},
                           {"image": "ja.png", "lines": [line("あい", 0, 0, 10, 10, 0.9), line("うえ", 0, 20, 10, 30, 0.9),
                                                         line("x", 0, 40, 10, 50, 0.5)]}]}
        korean = {"images": [{"image": "en.png", "lines": [line("Shift", 0, 0, 10, 10, 0.8), line("타", 0, 20, 10, 30, 0.6)]},
                             {"image": "ja.png", "lines": [line("아이", 0, 0, 10, 10, 0.5), line("우에", 0, 20, 10, 30, 0.5),
                                                           line("가", 0, 40, 10, 50, 0.6)]}]}
        combined = ev.vote_by_image(main, korean, script=True)
        self.assertEqual([[x["text"] for x in image["lines"]] for image in combined["images"]],
                         [["Shift", "타"], ["あい", "うえ", "x"]])

    def test_boxes_must_match(self):
        main = {"images": [{"image": "a.png", "lines": [line("a", 0, 0, 10, 10)]}]}
        korean = {"images": [{"image": "a.png", "lines": [line("b", 0, 0, 10, 11)]}]}
        with self.assertRaises(ValueError):
            ev.combine_korean(main, korean, ev.higher_score)


if __name__ == "__main__":
    unittest.main()
