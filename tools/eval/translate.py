"""M0-12：把正確答案的原文送到各翻譯引擎，結果給 rate_translations.py 盲評。

    tools/eval/.venv/Scripts/python tools/eval/translate.py --engines google gemini hy-mt2

引擎（和 design.md 4.5 的引擎清單對應）：
- `google`：Google 網頁翻譯的非官方端點，免費、不需要金鑰。一頁的段落用換行合併送出，
  回來後切開；數量對不上就逐段重送（和產品的做法相同）。每秒最多 1 個請求。
- `gemini`：Google AI Studio 的 API，需要環境變數 `GEMINI_API_KEY`（金鑰不會寫進任何檔案）。
  照 design.md 4.5 的提示詞，一頁一次，要求輸出和原文等長的 JSON 陣列。
- `claude`：Anthropic 的 Messages API（付費），需要環境變數 `ANTHROPIC_API_KEY`，提示詞和 gemini 相同；
  機構層級的金鑰還要 `ANTHROPIC_WORKSPACE_ID`（在 Console 的 workspace 網址裡，不是機密）。
- `hy-mt2`：本機 Ollama 上的 HY-MT2-7B（翻譯專用模型，一段一次，照官方的提示詞格式）。

所有譯文都用 OpenCC 的 s2twp 轉成台灣繁體（和產品相同）。另有含義的ルビ用 `{本文|讀音}` 標出來
（design.md 4.5）。結果寫在 build/translation_eval/m0-12/<引擎>.json，含有截圖裡的文字，不進版本
控制；已經翻過而且原文沒變的段落會沿用，加 --force 才重翻。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, asdict
from pathlib import Path

import ground_truth as gt

REPO_ROOT = Path(__file__).resolve().parents[2]
PRIVATE = REPO_ROOT / "testdata" / "private"
OUTPUT = REPO_ROOT / "build" / "translation_eval" / "m0-12"
CATEGORIES = [f"{language}-{kind}" for language in ("ja", "en", "ko")
              for kind in ("manga", "game", "web")]
MIN_LETTERS = 2  # 少於兩個字的區塊（數字、單一符號）不評測翻譯
LANGUAGE_NAMES = {"ja": "Japanese", "en": "English", "ko": "Korean"}
GOOGLE_CODES = {"ja": "ja", "en": "en", "ko": "ko"}
TIMEOUT = 120
RETRIES = 5
BACKOFF = 5  # 被限流（429）或模型忙碌（503）時，第 n 次重試前等 n×BACKOFF 秒

SYSTEM_PROMPT = (
    "你是翻譯引擎。把 segments 中的每個字串翻譯成台灣繁體中文，保留語氣和角色口吻。"
    "`{本文|讀音}` 是日文漫畫中作者刻意標的特殊讀音（左邊是字面，右邊是實際的意思），"
    "譯文要在對應的位置保留同樣的標記。"
    "只輸出和 segments 等長的 JSON 字串陣列，不要加任何說明。")

# 最後的退路：不要 JSON，只要譯文（模型偶爾會在譯文裡用沒有逸出的引號）
PLAIN_PROMPT = ("你是翻譯引擎。把使用者傳來的文字翻譯成台灣繁體中文，保留語氣和角色口吻。"
                "`{本文|讀音}` 標記要保留。只輸出譯文，不要加引號或任何說明。")


@dataclass
class Segment:
    category: str
    image: str
    index: int  # 區塊在這張截圖中的編號（從 1 起算）
    kind: str
    source: str


def marked_text(block: gt.Block, language: str) -> str:
    """區塊的文字；另有含義的ルビ標成 {本文|讀音}。"""
    text = block.text(language)
    position = 0
    for ruby in block.ruby:
        if not ruby.meaning:
            continue
        found = text.find(ruby.base, position)
        if found < 0:
            continue
        marked = f"{{{ruby.base}|{ruby.reading}}}"
        text = text[:found] + marked + text[found + len(ruby.base):]
        position = found + len(marked)
    return text


def load_segments(category: str) -> list[Segment]:
    language = category.split("-")[0]
    segments = []
    for page in gt.load(PRIVATE / category / "ground_truth.txt"):
        for i, block in enumerate(page.blocks, start=1):
            if block.excluded:
                continue
            text = marked_text(block, block.language or language)
            if len(re.findall(r"[^\W\d_]", text, re.UNICODE)) < MIN_LETTERS:
                continue
            segments.append(Segment(category, page.image, i, block.kind, text))
    return segments


def post(url: str, data: bytes | None, headers: dict[str, str]) -> bytes:
    """送出請求；HTTP 429、5xx 和連線錯誤會重試（每次等久一點）。"""
    for attempt in range(RETRIES):
        try:
            request = urllib.request.Request(url, data=data, headers=headers)
            with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
                return response.read()
        except urllib.error.HTTPError as error:
            if error.code not in (429, 500, 502, 503, 504, 529) or attempt == RETRIES - 1:
                raise RuntimeError(f"HTTP {error.code}: {error.read()[:200].decode('utf-8', 'replace')}")
        except (urllib.error.URLError, TimeoutError) as error:
            if attempt == RETRIES - 1:
                raise RuntimeError(str(error))
        time.sleep(BACKOFF * (attempt + 1))
    raise RuntimeError("unreachable")


# 每百萬 token 的價格（美元，2026-09 的公開價格），只用來估算花費
PRICES = {"claude-haiku-4-5-20251001": (1.0, 5.0)}


class BudgetError(RuntimeError):
    """估算的花費超過 --budget-usd：停下來並存檔。"""


class AlignmentError(RuntimeError):
    """LLM 的回應解析不出陣列：可以改成逐段重送。"""


# ---------------------------------------------------------------------------------------------
# 引擎：輸入一頁的原文，輸出等長的譯文（還沒轉成繁體）

class Google:
    """Google 網頁翻譯的非官方端點。"""

    id = "google"
    model = "translate_a/single client=gtx"
    interval = 1.0  # 每秒最多 1 個請求

    def translate(self, texts: list[str], language: str) -> list[str]:
        joined = self._request("\n".join(texts), language).split("\n")
        if len(joined) == len(texts):
            return joined
        return [self._request(text, language).replace("\n", " ") for text in texts]

    def _request(self, text: str, language: str) -> str:
        query = urllib.parse.urlencode({"client": "gtx", "sl": GOOGLE_CODES[language],
                                        "tl": "zh-TW", "dt": "t", "q": text})
        raw = post(f"https://translate.googleapis.com/translate_a/single?{query}", None,
                   {"User-Agent": "Mozilla/5.0"})
        pieces = json.loads(raw.decode("utf-8"))[0] or []
        return "".join(piece[0] for piece in pieces if piece and piece[0])


class LlmEngine:
    """要求 LLM 輸出等長 JSON 陣列的引擎（design.md 4.5 的對齊檢查）。

    三層退路：解析失敗或數量對不上時先重試同樣的請求（偶發的格式錯誤很常見），
    再改成一段一段送，最後一段還是不行就不要 JSON，直接請模型只輸出譯文
    （模型偶爾會在譯文裡用沒有逸出的引號，JSON 就壞了）。
    """

    ATTEMPTS = 2

    def translate(self, texts: list[str], language: str) -> list[str]:
        for _ in range(self.ATTEMPTS):
            try:
                result = self.request(texts, language)
            except AlignmentError:
                continue
            if len(result) == len(texts):
                return result
        if len(texts) == 1:
            return [self.complete(PLAIN_PROMPT, texts[0]).strip()]
        return [self.translate([text], language)[0] for text in texts]

    def request(self, texts: list[str], language: str) -> list[str]:
        payload = {"source_lang": LANGUAGE_NAMES[language], "segments": texts}
        return self.parse_array(self.complete(SYSTEM_PROMPT, json.dumps(payload,
                                                                        ensure_ascii=False)))

    def complete(self, system: str, user: str) -> str:
        """送出一次請求，回傳模型的文字。"""
        raise NotImplementedError

    @staticmethod
    def parse_array(answer: str) -> list[str]:
        """把回應解析成字串陣列；失敗時丟 AlignmentError。"""
        try:
            result = json.loads(answer)
        except json.JSONDecodeError as error:
            raise AlignmentError(f"回應不是 JSON：{error}；開頭 {answer[:60]!r}") from error
        if isinstance(result, dict):  # 有時候會包成 {"segments": [...]}
            result = next((v for v in result.values() if isinstance(v, list)), None)
        if not isinstance(result, list):
            raise AlignmentError(f"回應不是陣列：{answer[:60]!r}")
        return [str(x) for x in result]


class Gemini(LlmEngine):
    """Google AI Studio 的 API。金鑰只從環境變數讀取。

    免費額度會限流，所以每 2 秒才送一個請求；最新的預設模型常常忙碌（503），
    需要時用 --model 指定別的（M0-12 用 gemini-3.6-flash）。
    """

    id = "gemini"
    interval = 2.0

    def __init__(self, model: str | None = None):
        self.key = os.environ.get("GEMINI_API_KEY", "")
        if not self.key:
            raise RuntimeError("沒有設定環境變數 GEMINI_API_KEY")
        self.model = model or self.pick_model()

    def pick_model(self) -> str:
        """挑最新的 flash 版本（便宜、夠快，符合產品的預設）。"""
        raw = post(f"https://generativelanguage.googleapis.com/v1beta/models?key={self.key}",
                   None, {})
        names = [m["name"].removeprefix("models/") for m in json.loads(raw)["models"]
                 if "generateContent" in m.get("supportedGenerationMethods", [])]
        flash = [n for n in names if re.fullmatch(r"gemini-[\d.]+-flash", n)]
        if not flash:
            raise RuntimeError(f"找不到 flash 模型，可以用 --model 指定：{names}")
        return max(flash, key=lambda n: [int(p) for p in re.findall(r"\d+", n)])

    def complete(self, system: str, user: str) -> str:
        config = {"temperature": 0.2}
        if system is SYSTEM_PROMPT:  # 只有要 JSON 陣列時才限制輸出格式
            config["responseMimeType"] = "application/json"
        body = json.dumps({
            "systemInstruction": {"parts": [{"text": system}]},
            "contents": [{"role": "user", "parts": [{"text": user}]}],
            "generationConfig": config,
        }).encode("utf-8")
        raw = post(f"https://generativelanguage.googleapis.com/v1beta/models/"
                   f"{self.model}:generateContent?key={self.key}", body,
                   {"Content-Type": "application/json"})
        return json.loads(raw)["candidates"][0]["content"]["parts"][0]["text"]


class Claude(LlmEngine):
    """Anthropic 的 Messages API（付費）。金鑰只從環境變數讀取。

    要 JSON 陣列時先填一個「[」當開頭（prefill），模型就只會接著輸出其餘部分。
    """

    id = "claude"
    interval = 0.5
    DEFAULT_MODEL = "claude-haiku-4-5-20251001"

    def __init__(self, model: str | None = None):
        self.key = os.environ.get("ANTHROPIC_API_KEY", "")
        if not self.key:
            raise RuntimeError("沒有設定環境變數 ANTHROPIC_API_KEY")
        # 機構層級（沒有綁定 workspace）的金鑰要另外指定 workspace
        self.workspace = os.environ.get("ANTHROPIC_WORKSPACE_ID", "")
        self.model = model or self.DEFAULT_MODEL
        self.tokens = [0, 0]  # 累計的輸入、輸出 token
        self.budget = 0.0  # 大於 0 時，估算花費超過就停

    def complete(self, system: str, user: str) -> str:
        prefill = system is SYSTEM_PROMPT  # 只有要 JSON 陣列時才填開頭
        messages = [{"role": "user", "content": user}]
        if prefill:
            messages.append({"role": "assistant", "content": "["})
        body = json.dumps({"model": self.model, "max_tokens": 8192, "temperature": 0.2,
                           "system": system, "messages": messages}).encode("utf-8")
        headers = {"Content-Type": "application/json", "x-api-key": self.key,
                   "anthropic-version": "2023-06-01"}
        if self.workspace:
            headers["anthropic-workspace-id"] = self.workspace
        answer = json.loads(post("https://api.anthropic.com/v1/messages", body, headers))
        usage = answer.get("usage", {})
        self.tokens[0] += usage.get("input_tokens", 0)
        self.tokens[1] += usage.get("output_tokens", 0)
        if self.budget and self.cost() > self.budget:
            raise BudgetError(f"估算花費 {self.cost():.2f} 美元，超過上限 {self.budget:.2f}")
        text = answer["content"][0]["text"]
        if not prefill:
            return text
        text = "[" + text
        return text[:text.rindex("]") + 1] if "]" in text else text


    def cost(self) -> float:
        """目前為止的估算花費（美元）。"""
        price = PRICES.get(self.model)
        return 0.0 if not price else sum(t * p for t, p in zip(self.tokens, price)) / 1e6


class Ollama:
    """本機 Ollama 上的翻譯模型。HY-MT2 是翻譯專用模型，一段一次、照官方的提示詞。

    每段的字很少，一次只送一個請求的話 GPU 大半時間在等（解碼受限於記憶體頻寬），
    所以同一頁的段落同時送出：權重只讀一次就能服務多個序列，吞吐量幾乎等比例增加
    （Ollama 會依 OLLAMA_NUM_PARALLEL 決定實際的平行度，實測 8～16 之後就飽和）。

    網址一定要用 127.0.0.1：在 Windows 上用 localhost 時，每個請求會先試 IPv6 再退回 IPv4，
    每次多花約 2 秒（實測 2.2 秒對 0.15 秒）。
    """

    id = "hy-mt2"
    interval = 0.0
    PARALLEL = 8
    OPTIONS = {"temperature": 0.7, "top_p": 0.6, "top_k": 20, "repeat_penalty": 1.05,
               "num_predict": 4096}

    def __init__(self, model: str = "hy-mt2", host: str = "http://127.0.0.1:11434"):
        self.model, self.host = model, host
        self.parallel = self.PARALLEL

    def translate(self, texts: list[str], language: str) -> list[str]:
        if self.parallel <= 1 or len(texts) == 1:
            return [self._one(text) for text in texts]
        with ThreadPoolExecutor(max_workers=self.parallel) as pool:
            return list(pool.map(self._one, texts))

    def _one(self, text: str) -> str:
        prompt = f"将以下文本翻译成繁体中文，注意只需要输出翻译后的结果，不要额外解释:\n\n{text}"
        body = json.dumps({"model": self.model, "stream": False, "options": self.OPTIONS,
                           "messages": [{"role": "user", "content": prompt}]}).encode("utf-8")
        raw = post(f"{self.host}/api/chat", body, {"Content-Type": "application/json"})
        return json.loads(raw)["message"]["content"].strip()


ENGINES = {"google": Google, "gemini": Gemini, "claude": Claude, "hy-mt2": Ollama}


# ---------------------------------------------------------------------------------------------

def key_of(segment: Segment) -> str:
    return f"{segment.category}/{segment.image}#{segment.index}"


def needs_work(row: dict | None, segment: Segment) -> bool:
    """沒翻過、原文改了，或上次失敗（錯誤或空譯文）的段落要重翻。"""
    return (row is None or row["source"] != segment.source or bool(row["error"])
            or not row["translation"].strip())


def save(path: Path, name: str, engine, rows: list[dict]) -> None:
    path.write_text(json.dumps(
        {"engine": name, "model": getattr(engine, "model", ""),
         "created": time.strftime("%Y-%m-%d %H:%M"), "segments": rows},
        ensure_ascii=False, indent=1), encoding="utf-8", newline="\n")


def load_previous(path: Path) -> dict[str, dict]:
    if not path.exists():
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    return {row["key"]: row for row in data["segments"]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--engines", nargs="+", default=["google"], choices=sorted(ENGINES))
    parser.add_argument("--categories", nargs="+", default=CATEGORIES)
    parser.add_argument("--model", help="覆寫引擎預設的模型名稱")
    parser.add_argument("--force", action="store_true", help="已經翻過的段落也重翻")
    parser.add_argument("--interval", type=float, help="兩個請求之間至少等幾秒（被限流時調大）")
    parser.add_argument("--parallel", type=int, help="本機模型同時送出幾個請求")
    parser.add_argument("--budget-usd", type=float, default=2.0,
                        help="付費引擎的估算花費上限（美元），超過就停下來存檔")
    args = parser.parse_args()

    import opencc  # 只有真的要翻譯時才需要（測試只會用到上面的函式）

    converter = opencc.OpenCC("s2twp")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name in args.engines:
        engine = ENGINES[name](args.model) if args.model else ENGINES[name]()
        if args.interval is not None:
            engine.interval = args.interval
        if args.parallel is not None and hasattr(engine, "parallel"):
            engine.parallel = args.parallel
        if hasattr(engine, "budget"):
            engine.budget = args.budget_usd
        path = OUTPUT / f"{name}.json"
        existing = load_previous(path)  # 只跑部分分類時，其他分類的結果要保留
        previous = {} if args.force else existing
        results, last = dict(existing), 0.0
        for category in args.categories:
            language = category.split("-")[0]
            segments = load_segments(category)
            pages = {}
            for segment in segments:
                pages.setdefault(segment.image, []).append(segment)
            for image, page in pages.items():
                todo = [s for s in page if needs_work(previous.get(key_of(s)), s)]
                if not todo:
                    continue
                time.sleep(max(0.0, engine.interval - (time.perf_counter() - last)))
                start = time.perf_counter()
                try:
                    translations = engine.translate([s.source for s in todo], language)
                    error = ""
                except BudgetError as stop:
                    save(path, name, engine, list(results.values()))
                    raise SystemExit(f"{name}：{stop}（已經翻好的都存檔了）") from stop
                except RuntimeError as failure:
                    translations, error = [""] * len(todo), str(failure)
                milliseconds = (time.perf_counter() - start) * 1000
                last = time.perf_counter()
                for segment, raw in zip(todo, translations):
                    results[key_of(segment)] = {
                        "key": key_of(segment), **asdict(segment), "raw": raw,
                        "translation": converter.convert(raw), "error": error,
                        "ms": round(milliseconds / len(todo), 1)}
                print(f"{name:7} {category:9} {image[-10:]} {len(todo):3} 段 "
                      f"{milliseconds / 1000:6.1f} 秒 {error}", flush=True)
                save(path, name, engine, list(results.values()))  # 每頁都存，中途停掉也不會白跑
        save(path, name, engine, list(results.values()))
        failures = sum(1 for row in results.values() if row["error"])
        spent = (f"，這次用了 {engine.tokens[0]}＋{engine.tokens[1]} token"
                 f"（約 {engine.cost():.2f} 美元）" if getattr(engine, "tokens", None) else "")
        print(f"{name}：{len(results)} 段，失敗 {failures} 段{spent} → {path}")


if __name__ == "__main__":
    main()
