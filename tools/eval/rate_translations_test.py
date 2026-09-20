"""rate_translations.py 的測試（只用標準函式庫，ctest 會執行）。"""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import rate_translations as rate


def rows(keys: dict[str, str], translation: str = "譯文") -> dict[str, dict]:
    """key → 該段的結果。keys 是 key → 原文。"""
    return {key: {"key": key, "category": key.split("/")[0], "image": key.split("/")[1].split("#")[0],
                  "index": int(key.split("#")[1]), "kind": "對白", "source": source,
                  "translation": translation, "error": ""}
            for key, source in keys.items()}


def engines(count: int = 12) -> dict[str, dict[str, dict]]:
    keys = {f"ja-manga/p{i}.png#1": f"原文{i}" for i in range(count)}
    keys["ja-manga/ruby.png#1"] = "{本気|マジ}で"
    keys["en-web/p0.png#1"] = "SOURCE"
    return {"google": rows(keys, "google 的譯文"), "gemini": rows(keys, "gemini 的譯文")}


class SampleTest(unittest.TestCase):
    def test_picks_per_category_and_always_includes_marked(self):
        keys = rate.sample_keys(engines(), per_category=3)
        self.assertEqual(len([k for k in keys if k.startswith("ja-manga/")]), 3)
        self.assertEqual([k for k in keys if k.startswith("en-web/")], ["en-web/p0.png#1"])
        self.assertIn("ja-manga/ruby.png#1", keys)
        self.assertEqual(keys, rate.sample_keys(engines(), per_category=3))  # 每次都一樣

    def test_skips_segments_an_engine_is_missing_or_left_empty(self):
        data = engines(4)
        del data["gemini"]["ja-manga/p0.png#1"]
        data["gemini"]["ja-manga/p1.png#1"] = {**data["gemini"]["ja-manga/p1.png#1"],
                                               "translation": " "}
        keys = rate.sample_keys(data, per_category=8)
        self.assertNotIn("ja-manga/p0.png#1", keys)
        self.assertNotIn("ja-manga/p1.png#1", keys)
        self.assertIn("ja-manga/p2.png#1", keys)


class BuildItemsTest(unittest.TestCase):
    def test_order_is_shuffled_per_segment_but_stable(self):
        data = {name: rows({f"ja-manga/p{i}.png#1": "原文" for i in range(20)}, name)
                for name in ("a", "b", "c")}
        items = rate.build_items(data, rate.sample_keys(data, per_category=20))
        self.assertEqual({tuple(sorted(i.order)) for i in items}, {("a", "b", "c")})
        self.assertGreater(len({tuple(i.order) for i in items}), 1)  # 不是每一段都同樣順序
        again = rate.build_items(data, [items[0].key])
        self.assertEqual(again[0].order, items[0].order)
        self.assertEqual(items[0].translations, {"a": "a", "b": "b", "c": "c"})


class RatingsFileTest(unittest.TestCase):
    def test_round_trip_and_changed_source_drops_scores(self):
        data = engines(2)
        items = rate.build_items(data, rate.sample_keys(data, per_category=8))
        items[0].scores = {"google": 4, "gemini": 2}
        items[0].note = "漏了語氣"
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "ratings.json"
            rate.save_ratings(items, path)
            self.assertEqual(len(json.loads(path.read_text(encoding="utf-8"))["items"]), 1)

            again = rate.build_items(data, [i.key for i in items])
            rate.load_ratings(again, path)
            self.assertEqual(again[0].scores, {"google": 4, "gemini": 2})
            self.assertEqual(again[0].note, "漏了語氣")

            changed = rate.build_items(data, [items[0].key])
            changed[0].source = "改過的原文"
            rate.load_ratings(changed, path)
            self.assertEqual(changed[0].scores, {})


if __name__ == "__main__":
    unittest.main()
