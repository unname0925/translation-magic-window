"""ocr_lines.py 的測試（只用標準函式庫，ctest 會執行）。"""

from __future__ import annotations

import unittest

import ocr_lines


def ocr_line(text: str, x0: int, y0: int, x1: int, y1: int) -> dict:
    return {"text": text, "score": 1.0, "box": [[x0, y0], [x1, y0], [x1, y1], [x0, y1]]}


class OrderOcrLinesTest(unittest.TestCase):
    def test_korean_words_on_one_row_join_with_space(self):
        lines = [ocr_line("들립니다.", 300, 0, 400, 20), ocr_line("천둥", 0, 2, 50, 21),
                 ocr_line("치는", 60, 1, 110, 20), ocr_line("소리가", 120, 0, 290, 22),
                 ocr_line("다음 줄", 0, 40, 100, 60), ocr_line(" ", 0, 80, 10, 90)]
        self.assertEqual(ocr_lines.order_ocr_lines(lines, "horizontal", "ko"),
                         ["천둥 치는 소리가 들립니다.", "다음 줄"])

    def test_japanese_joins_without_space(self):
        lines = [ocr_line("です", 60, 0, 100, 20), ocr_line("日本語", 0, 0, 55, 20)]
        self.assertEqual(ocr_lines.order_ocr_lines(lines, "horizontal", "ja"), ["日本語です"])

    def test_vertical_columns_right_to_left(self):
        lines = [ocr_line("二行目", 0, 0, 20, 100), ocr_line("一行", 40, 0, 60, 45),
                 ocr_line("目", 42, 50, 58, 70)]
        self.assertEqual(ocr_lines.order_ocr_lines(lines, "vertical", "ja"), ["一行目", "二行目"])


class FindRubyTest(unittest.TestCase):
    def test_vertical_ruby_on_the_right(self):
        lines = ocr_lines.to_lines([ocr_line("東京", 100, 0, 130, 60),  # 本文：寬 30
                                    ocr_line("とうきょう", 131, 2, 142, 58),  # ルビ：寬 11，貼在右側
                                    ocr_line("大阪", 40, 0, 70, 60)])  # 另一行本文
        ocr_lines.find_ruby(lines)
        self.assertEqual([line.ruby_of for line in lines], [None, 0, None])
        self.assertEqual(lines[0].rubies, [1])

    def test_horizontal_ruby_above(self):
        lines = ocr_lines.to_lines([ocr_line("ほんき", 0, 0, 40, 8),
                                    ocr_line("本気", 0, 9, 40, 29)])
        ocr_lines.find_ruby(lines)
        self.assertEqual([line.ruby_of for line in lines], [1, None])

    def test_not_ruby(self):
        cases = {
            "太細的行不夠細": [ocr_line("東京", 100, 0, 130, 60), ocr_line("とうきょう", 131, 0, 152, 60)],
            "離太遠": [ocr_line("東京", 100, 0, 130, 60), ocr_line("とうきょう", 160, 0, 171, 60)],
            "在左邊（直排的ルビ在右側）": [ocr_line("東京", 100, 0, 130, 60),
                                        ocr_line("とうきょう", 88, 0, 99, 60)],
        }
        for name, items in cases.items():
            with self.subTest(name):
                lines = ocr_lines.to_lines(items)
                ocr_lines.find_ruby(lines)
                self.assertEqual([line.ruby_of for line in lines], [None, None])

    def test_empty_text_is_skipped(self):
        lines = ocr_lines.to_lines([ocr_line("", 0, 0, 10, 10), ocr_line("字", 0, 20, 10, 30)])
        self.assertEqual([(line.index, line.text) for line in lines], [(1, "字")])


if __name__ == "__main__":
    unittest.main()
