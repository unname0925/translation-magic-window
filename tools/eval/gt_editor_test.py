"""gt_editor.py 不需要視窗的部分的測試（只用標準函式庫，ctest 會執行）。"""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import ground_truth as gt
import gt_editor as editor

FILE = """# 標題註解
# 第二行

== b.png

[對白 直 中 10,10,50,90]
本気で
ルビ: 本気=マジ!
"""


def block(box=(0, 0, 10, 10), lines=("字",)) -> gt.Block:
    return gt.Block("對白", "horizontal", "normal", box, list(lines))


class DocumentTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = Path(self.folder.name)
        category = self.root / "ja-manga"
        category.mkdir()
        for name in ("a.png", "b.png", "c.webp", "notes.txt"):
            (category / name).write_bytes(b"")
        self.path = category / "ground_truth.txt"
        self.path.write_text(FILE, encoding="utf-8")

    def tearDown(self):
        self.folder.cleanup()

    def test_split_header(self):
        header, body = editor.split_header(FILE)
        self.assertEqual(header, ["# 標題註解", "# 第二行"])
        self.assertTrue(body.startswith("== b.png\n"))
        self.assertEqual(editor.split_header("# 只有註解\n\n"), (["# 只有註解"], ""))

    def test_load_keeps_order_and_adds_unannotated_images(self):
        doc = editor.load_document("ja-manga", self.root)
        self.assertEqual([p.image for p in doc.pages], ["b.png", "a.png", "c.webp"])
        self.assertEqual(len(doc.pages[0].blocks), 1)
        self.assertEqual(doc.pages[1].blocks, [])

    def test_load_without_file(self):
        self.path.unlink()
        doc = editor.load_document("ja-manga", self.root)
        self.assertEqual(doc.header, editor.DEFAULT_HEADER)
        self.assertEqual(len(doc.pages), 3)

    def test_save_keeps_header_uses_lf_and_backs_up_once(self):
        doc = editor.load_document("ja-manga", self.root)
        doc.pages[1].blocks.append(block(lines=["新的"]))
        self.assertEqual(editor.document_problems(doc), [])
        editor.save_document(doc, backup=True)
        saved = self.path.read_bytes()
        self.assertNotIn(b"\r", saved)
        self.assertTrue(saved.decode("utf-8").startswith("# 標題註解\n# 第二行\n\n== b.png\n"))
        self.assertEqual(editor.load_document("ja-manga", self.root).pages, doc.pages)
        backup = self.path.with_name("ground_truth.txt.bak")
        self.assertEqual(backup.read_text(encoding="utf-8"), FILE)
        editor.save_document(doc, backup=False)
        self.assertEqual(backup.read_text(encoding="utf-8"), FILE)
        self.assertFalse(self.path.with_name("ground_truth.txt.tmp").exists())

    def test_problems_point_to_page_and_block(self):
        doc = editor.load_document("ja-manga", self.root)
        doc.pages[2].blocks += [block(), block(lines=[])]
        self.assertEqual(editor.document_problems(doc), [(2, 1, "沒有文字")])

    def test_draft_blocks(self):
        drafts = self.root / "drafts"
        (drafts / "ja-manga").mkdir(parents=True)
        (drafts / "ja-manga" / "ground_truth.draft.txt").write_text(
            "== a.png\n\n[對白 橫 中 1,2,3,4]\n草稿\n備註: #1\n", encoding="utf-8")
        blocks = editor.draft_blocks("ja-manga", "a.png", drafts)
        self.assertEqual([b.lines for b in blocks], [["草稿"]])
        self.assertEqual(editor.draft_blocks("ja-manga", "b.png", drafts), [])
        self.assertEqual(editor.draft_blocks("ko-web", "a.png", drafts), [])


class BlockProblemsTest(unittest.TestCase):
    def test_valid(self):
        self.assertEqual(editor.block_problems(block(lines=["#今日のおすすめ"])), [])

    def test_problems(self):
        cases = {
            "框是空的": block(box=(5, 5, 5, 9)),
            "沒有文字": block(lines=[]),
            "會被當成格式": block(lines=["ルビ: 東=とう"]),
            "依序找不到": gt.Block("對白", "vertical", "normal", (0, 0, 1, 1), ["東京"],
                              [gt.Ruby("京", "きょう"), gt.Ruby("東", "とう")]),
        }
        for expected, case in cases.items():
            with self.subTest(expected):
                problems = editor.block_problems(case)
                self.assertEqual(len(problems), 1)
                self.assertIn(expected, problems[0])


class RubyFieldTest(unittest.TestCase):
    def test_round_trip(self):
        ruby = editor.parse_ruby(" 皆=みな  本気=マジ! ")
        self.assertEqual([(r.base, r.reading, r.meaning) for r in ruby],
                         [("皆", "みな", False), ("本気", "マジ", True)])
        self.assertEqual(editor.format_ruby(ruby), "皆=みな 本気=マジ!")
        self.assertEqual(editor.parse_ruby(""), [])

    def test_errors(self):
        for text in ("皆", "=みな", "皆=", "皆=!"):
            with self.subTest(text):
                with self.assertRaises(ValueError):
                    editor.parse_ruby(text)


class GeometryTest(unittest.TestCase):
    def test_normalize_orders_and_clamps(self):
        self.assertEqual(editor.normalize_box(50, 40, 10.4, -5, 45, 100), (10, 0, 45, 40))

    def test_move_stays_inside_image(self):
        self.assertEqual(editor.move_box((10, 10, 30, 20), 5, 5, 100, 100), (15, 15, 35, 25))
        self.assertEqual(editor.move_box((10, 10, 30, 20), 500, -50, 100, 100), (80, 0, 100, 10))

    def test_hit_handle(self):
        box = (100, 100, 200, 150)
        self.assertEqual(editor.hit_handle(box, 101, 99, 3), "nw")
        self.assertEqual(editor.hit_handle(box, 199, 152, 3), "se")
        self.assertEqual(editor.hit_handle(box, 150, 100, 3), "n")
        self.assertEqual(editor.hit_handle(box, 202, 125, 3), "e")
        self.assertEqual(editor.hit_handle(box, 150, 125, 3), "")  # 框的中間
        self.assertEqual(editor.hit_handle(box, 150, 90, 3), "")  # 框外

    def test_resize(self):
        box = (100, 100, 200, 150)
        self.assertEqual(editor.resize_box(box, "se", 10, -20, 1000, 1000), (100, 100, 210, 130))
        self.assertEqual(editor.resize_box(box, "w", 150, 0, 1000, 1000), (200, 100, 250, 150))
        self.assertEqual(editor.resize_box(box, "n", 0, -500, 1000, 1000), (100, 0, 200, 150))

    def test_block_at_prefers_smallest(self):
        blocks = [block(box=(0, 0, 100, 100)), block(box=(10, 10, 30, 30)), block(box=(200, 0, 210, 5))]
        self.assertEqual(editor.block_at(blocks, 20, 20), 1)
        self.assertEqual(editor.block_at(blocks, 50, 50), 0)
        self.assertIsNone(editor.block_at(blocks, 150, 50))

    def test_guess_direction(self):
        self.assertEqual(editor.guess_direction((0, 0, 20, 100)), "vertical")
        self.assertEqual(editor.guess_direction((0, 0, 100, 20)), "horizontal")


if __name__ == "__main__":
    unittest.main()
