"""check_synthetic.py 的字元錯誤率算法（只用標準函式庫，CI 會跑）。"""

import json
import unittest
from pathlib import Path

from check_synthetic import SYNTHETIC, character_error_rate


class CharacterErrorRateTest(unittest.TestCase):
    def test_a_perfect_read_is_zero(self):
        self.assertEqual(character_error_rate(["本気で", "言ってる"], ["本気で言ってる"]), 0.0)

    def test_line_order_and_spaces_do_not_count(self):
        # 閱讀順序和分段另外有測試；這裡只看字有沒有讀對
        self.assertEqual(character_error_rate(["Hello world", "Bye"], ["Bye", "Helloworld"]), 0.0)

    def test_full_width_and_half_width_digits_are_the_same(self):
        self.assertEqual(character_error_rate(["所持金：１２，３４５"], ["所持金:12,345"]), 0.0)

    def test_a_wrong_character_is_one_error(self):
        self.assertAlmostEqual(character_error_rate(["あいうえ"], ["あいうお"]), 0.25)

    def test_a_missing_or_extra_character_is_one_error(self):
        self.assertAlmostEqual(character_error_rate(["あいうえ"], ["あいう"]), 0.25)
        self.assertAlmostEqual(character_error_rate(["あいうえ"], ["あいうえお"]), 0.25)

    def test_reading_nothing_is_all_wrong(self):
        self.assertEqual(character_error_rate(["あいうえ"], []), 1.0)


class CommittedDataTest(unittest.TestCase):
    def test_every_expected_image_exists_and_has_a_baseline(self):
        expected = json.loads((SYNTHETIC / "expected.json").read_text(encoding="utf-8"))
        baseline = json.loads((SYNTHETIC / "baseline.json").read_text(encoding="utf-8"))
        for name, entry in expected.items():
            self.assertTrue(Path(SYNTHETIC / name).exists(), name)
            self.assertIn(entry["language"], ("ja", "en", "ko"))
            for key in ("cpu/medium", "cpu/small"):
                self.assertIn(name, baseline[key], f"{key} 沒有 {name} 的基準線")


if __name__ == "__main__":
    unittest.main()
