"""translate.py 的測試（只用標準函式庫，ctest 會執行；不會連到任何服務）。"""

from __future__ import annotations

import unittest
import unittest.mock

import ground_truth as gt
import translate as tr


def block(lines: list[str], ruby: list[gt.Ruby] | None = None) -> gt.Block:
    return gt.Block("對白", "vertical", "normal", (0, 0, 10, 10), lines, ruby=ruby or [])


class MarkedTextTest(unittest.TestCase):
    def test_only_meaningful_ruby_is_marked(self):
        b = block(["本気で戦う"], [gt.Ruby("本気", "マジ", meaning=True)])
        self.assertEqual(tr.marked_text(b, "ja"), "{本気|マジ}で戦う")
        plain = block(["東京へ"], [gt.Ruby("東京", "とうきょう")])
        self.assertEqual(tr.marked_text(plain, "ja"), "東京へ")

    def test_marks_each_occurrence_in_order(self):
        b = block(["東京と東京"], [gt.Ruby("東京", "とうきょう", meaning=True),
                                   gt.Ruby("東京", "みやこ", meaning=True)])
        self.assertEqual(tr.marked_text(b, "ja"), "{東京|とうきょう}と{東京|みやこ}")

    def test_english_lines_join_with_space(self):
        self.assertEqual(tr.marked_text(block(["HELLO", "WORLD"]), "en"), "HELLO WORLD")


class NeedsWorkTest(unittest.TestCase):
    def setUp(self):
        self.segment = tr.Segment("ja-manga", "a.png", 1, "對白", "原文")
        self.row = {"source": "原文", "translation": "譯文", "error": ""}

    def test_done_segment_is_skipped(self):
        self.assertFalse(tr.needs_work(self.row, self.segment))

    def test_missing_changed_failed_or_empty_are_redone(self):
        self.assertTrue(tr.needs_work(None, self.segment))
        self.assertTrue(tr.needs_work({**self.row, "source": "改過的原文"}, self.segment))
        self.assertTrue(tr.needs_work({**self.row, "error": "HTTP 429"}, self.segment))
        self.assertTrue(tr.needs_work({**self.row, "translation": "  "}, self.segment))


class LlmFallbackTest(unittest.TestCase):
    """LLM 回傳的數量和原文對不上時，改成一段一段送。"""

    class Engine(tr.LlmEngine):
        def __init__(self, drop: bool):
            self.drop, self.calls, self.plain = drop, [], []

        def request(self, texts, language):
            self.calls.append(list(texts))
            if self.drop and len(texts) > 1:
                return [f"譯：{t}" for t in texts[:-1]]  # 少一段
            return [f"譯：{t}" for t in texts]

        def complete(self, system, user):
            self.plain.append(user)
            return f"純文字：{user}"

    def test_batch_is_kept_when_the_count_matches(self):
        engine = self.Engine(drop=False)
        self.assertEqual(engine.translate(["一", "二"], "ja"), ["譯：一", "譯：二"])
        self.assertEqual(engine.calls, [["一", "二"]])

    def test_falls_back_to_one_at_a_time(self):
        engine = self.Engine(drop=True)
        self.assertEqual(engine.translate(["一", "二"], "ja"), ["譯：一", "譯：二"])
        # 同樣的請求先重試一次（格式錯誤常常是偶發的），還是不對才一段一段送
        self.assertEqual(engine.calls, [["一", "二"], ["一", "二"], ["一"], ["二"]])

    def test_parse_failure_also_falls_back(self):
        class Broken(tr.LlmEngine):
            def __init__(self):
                self.calls = []

            def request(self, texts, language):
                self.calls.append(list(texts))
                if len(texts) > 1:
                    return self.parse_array('["少了逗號" "壞掉的 JSON"]')
                return [f"譯：{texts[0]}"]

            def complete(self, system, user):
                raise AssertionError("逐段送成功時不該用到純文字的退路")

        engine = Broken()
        self.assertEqual(engine.translate(["一", "二"], "ja"), ["譯：一", "譯：二"])
        self.assertEqual(engine.calls, [["一", "二"], ["一", "二"], ["一"], ["二"]])

    def test_last_resort_asks_for_plain_text(self):
        """一段一段送還是解析不出來時，改成只要譯文（不用 JSON）。"""

        class AlwaysBroken(tr.LlmEngine):
            def __init__(self):
                self.plain = []

            def request(self, texts, language):
                raise tr.AlignmentError("壞掉的 JSON")

            def complete(self, system, user):
                self.plain.append((system, user))
                return "  純文字譯文  "

        engine = AlwaysBroken()
        self.assertEqual(engine.translate(["一", "二"], "ja"), ["純文字譯文", "純文字譯文"])
        self.assertEqual([user for _, user in engine.plain], ["一", "二"])
        self.assertEqual({system for system, _ in engine.plain}, {tr.PLAIN_PROMPT})

    def test_parse_array_accepts_a_wrapped_list(self):
        self.assertEqual(tr.LlmEngine.parse_array('{"segments": ["甲", "乙"]}'), ["甲", "乙"])
        with self.assertRaises(tr.AlignmentError):
            tr.LlmEngine.parse_array('"不是陣列"')

    def test_other_errors_are_not_retried(self):
        class Failing(tr.LlmEngine):
            def request(self, texts, language):
                raise RuntimeError("HTTP 401")

            def complete(self, system, user):
                raise RuntimeError("HTTP 401")

        with self.assertRaises(RuntimeError):
            Failing().translate(["一", "二"], "ja")


class CostTest(unittest.TestCase):
    """付費引擎要能估算花費，並在超過上限時停下來。"""

    def engine(self) -> tr.Claude:
        with unittest.mock.patch.dict("os.environ", {"ANTHROPIC_API_KEY": "測試用"}):
            return tr.Claude()

    def test_cost_uses_the_price_table(self):
        engine = self.engine()
        engine.tokens = [1_000_000, 200_000]
        price_in, price_out = tr.PRICES[engine.model]
        self.assertAlmostEqual(engine.cost(), price_in + 0.2 * price_out)

    def test_unknown_model_has_no_estimate(self):
        engine = self.engine()
        engine.model = "某個還沒列價格的模型"
        engine.tokens = [1_000_000, 1_000_000]
        self.assertEqual(engine.cost(), 0.0)


class GoogleBatchTest(unittest.TestCase):
    """一頁的段落用換行合併送出；數量對不上時逐段重送。"""

    def setUp(self):
        self.engine = tr.Google()
        self.sent: list[str] = []

    def fake(self, answer):
        def _request(text, language):
            self.sent.append(text)
            return answer(text)
        self.engine._request = _request

    def test_batch_is_used_when_the_line_count_matches(self):
        self.fake(lambda text: "\n".join(f"譯：{line}" for line in text.split("\n")))
        self.assertEqual(self.engine.translate(["一", "二"], "ja"), ["譯：一", "譯：二"])
        self.assertEqual(self.sent, ["一\n二"])

    def test_falls_back_to_one_at_a_time_and_flattens_newlines(self):
        def answer(text):
            return "少了一行" if "\n" in text else f"譯：{text}\n第二行"
        self.fake(answer)
        self.assertEqual(self.engine.translate(["一", "二"], "ja"),
                         ["譯：一 第二行", "譯：二 第二行"])
        self.assertEqual(self.sent, ["一\n二", "一", "二"])


if __name__ == "__main__":
    unittest.main()
