"""M0-11：OCR 評測。把 run_ocr.py 的結果和正確答案比較，算出指標並寫成報告。

    python tools/eval/evaluate_ocr.py

報告寫在 build/ocr_eval/m0-11/report.md（含有截圖裡的文字，不進版本控制）。只用標準函式庫。

對應方式：辨識結果的每一行（文字框）歸給和它重疊最多的區塊（重疊面積要超過這行的一半）；
歸到「不評測」區塊的行不計。一個區塊的辨識結果＝歸給它的行依閱讀順序接起來（ocr_lines.order_ocr_lines）。
日文先用和產品相同的規則（ocr_lines.find_ruby）把ルビ行分出來，只和本文比較。
語言和整個分類不同的區塊（例如英文漫畫裡的日文擬聲詞）另外統計，不算進主要指標。

指標：
- 字元錯誤率（CER）：編輯距離總和 ÷ 正確答案的字數總和。兩邊都先用 ground_truth.normalize
  正規化（全形半形、刪節號、引號統一，去掉空白），所以英文、韓文的空白不計。
- 完全正確：辨識結果和正確答案完全相同的區塊比例。
- 漏掉：沒有任何一行對應到的區塊比例（偵測失敗）。
- 多出：沒有落在任何區塊裡的行的字數（例如把圖案認成字），以每張截圖平均。
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
from dataclasses import dataclass, field
from pathlib import Path

import ground_truth as gt
from ocr_lines import find_ruby, order_ocr_lines, to_lines

REPO_ROOT = Path(__file__).resolve().parents[2]
PRIVATE = REPO_ROOT / "testdata" / "private"
EVAL = REPO_ROOT / "build" / "ocr_eval" / "m0-11"
RAW = EVAL / "raw"

MAIN_COMBOS = ("v6-medium", "v6-small", "v6-tiny", "v5-server", "v5-mobile", "windows-ocr")
KOREAN_COMBOS = ("ko-v6-medium-det", "ko-v6-small-det", "ko-v5-server-det", "ko-v5-mobile-det")
# 結果不在 raw/dml 的組合：Windows OCR（windows_ocr.ps1）
RAW_DEVICE = {"windows-ocr": "winrt"}
# 同一個偵測模型：主模型 → 韓文模型（文字框完全相同，可以逐行比較）
KOREAN_PAIRS = {"v6-medium": "ko-v6-medium-det", "v6-small": "ko-v6-small-det",
                "v5-server": "ko-v5-server-det", "v5-mobile": "ko-v5-mobile-det"}


# ---------------------------------------------------------------------------------------------
# 基本計算

def levenshtein(a: str, b: str) -> int:
    if len(a) < len(b):
        a, b = b, a
    previous = list(range(len(b) + 1))
    for i, ca in enumerate(a, start=1):
        current = [i]
        for j, cb in enumerate(b, start=1):
            current.append(min(previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (ca != cb)))
        previous = current
    return previous[-1]


def line_rect(item: dict) -> tuple[float, float, float, float]:
    xs = [p[0] for p in item["box"]]
    ys = [p[1] for p in item["box"]]
    return min(xs), min(ys), max(xs), max(ys)


def intersection(a: tuple, b: tuple) -> float:
    return max(0, min(a[2], b[2]) - max(a[0], b[0])) * max(0, min(a[3], b[3]) - max(a[1], b[1]))


def assign_lines(blocks: list[gt.Block], items: list[dict]) -> tuple[dict[int, list[dict]], list[dict]]:
    """每一行歸給重疊最多的區塊（重疊超過這行面積的一半）；沒有文字的行略過。"""
    assigned: dict[int, list[dict]] = {}
    unassigned = []
    for item in items:
        if not item["text"].strip():
            continue
        rect = line_rect(item)
        area = max(1e-6, (rect[2] - rect[0]) * (rect[3] - rect[1]))
        best, best_overlap = None, 0.5 * area
        for i, block in enumerate(blocks):
            overlap = intersection(rect, block.box)
            if overlap > best_overlap:
                best, best_overlap = i, overlap
        if best is None:
            unassigned.append(item)
        else:
            assigned.setdefault(best, []).append(item)
    return assigned, unassigned


def block_hypothesis(block: gt.Block, items: list[dict], language: str) -> str:
    """歸給這個區塊的行依閱讀順序接起來；日文去掉ルビ行。"""
    if language == "ja":
        lines = to_lines(items)
        find_ruby(lines)
        items = [items[line.index] for line in lines if line.ruby_of is None]
    separator = " " if language in ("en", "ko") else ""
    return separator.join(order_ocr_lines(items, block.direction, language))


# ---------------------------------------------------------------------------------------------
# 評測

@dataclass
class Record:
    category: str
    image: str
    index: int  # 區塊在這張截圖中的編號（從 1 起算）
    kind: str
    direction: str
    size: str
    language: str
    reference: str  # 正規化後
    hypothesis: str  # 正規化後
    detected: bool

    @property
    def distance(self) -> int:
        return levenshtein(self.reference, self.hypothesis)


@dataclass
class Result:
    records: list[Record] = field(default_factory=list)
    extra_chars: int = 0  # 沒有落在任何區塊裡的字數
    pages: int = 0
    timings: list[float] = field(default_factory=list)  # 每張截圖偵測＋辨識的毫秒數


def evaluate(category: str, pages: list[gt.Page], ocr: dict) -> Result:
    language = category.split("-")[0]
    images = {image["image"]: image for image in ocr["images"]}
    result = Result()
    for page in pages:
        image = images.get(page.image)
        if image is None:
            continue
        result.pages += 1
        timings = image.get("timings_ms", {})
        result.timings.append(timings.get("detection", 0) + timings.get("recognition", 0))
        assigned, unassigned = assign_lines(page.blocks, image["lines"])
        result.extra_chars += sum(len(gt.normalize(item["text"])) for item in unassigned)
        for i, block in enumerate(page.blocks):
            if block.excluded:
                continue
            block_language = block.language or language
            items = assigned.get(i, [])
            result.records.append(Record(
                category, page.image, i + 1, block.kind, block.direction, block.size,
                block_language, gt.normalize(block.text(block_language)),
                gt.normalize(block_hypothesis(block, items, block_language)), bool(items)))
    return result


@dataclass
class Summary:
    blocks: int
    characters: int
    cer: float
    exact: float
    missed: float

    @staticmethod
    def of(records: list[Record]) -> Summary | None:
        if not records:
            return None
        characters = sum(len(r.reference) for r in records)
        return Summary(len(records), characters,
                       sum(r.distance for r in records) / max(1, characters),
                       sum(r.reference == r.hypothesis for r in records) / len(records),
                       sum(not r.detected for r in records) / len(records))


def main_records(result: Result) -> list[Record]:
    """語言和分類相同的區塊（主要指標）。"""
    return [r for r in result.records if r.language == r.category.split("-")[0]]


# ---------------------------------------------------------------------------------------------
# 語言判斷策略（design.md 4.4）：主模型的分數低於門檻時改用韓文模型

def combine_korean(main: dict, korean: dict, choose) -> dict:
    """逐行選擇主模型或韓文模型的結果。兩者的偵測模型相同，文字框必須完全一致。"""
    combined = {"images": []}
    korean_images = {image["image"]: image for image in korean["images"]}
    for image in main["images"]:
        other = korean_images[image["image"]]
        if [line["box"] for line in image["lines"]] != [line["box"] for line in other["lines"]]:
            raise ValueError(f"{image['image']}：兩次的文字框不同，無法逐行比較")
        lines = [b if choose(a, b) else a for a, b in zip(image["lines"], other["lines"])]
        combined["images"].append({**image, "lines": lines})
    return combined


def threshold_strategy(threshold: float):
    def choose(main_line: dict, korean_line: dict) -> bool:
        return main_line["score"] < threshold and korean_line["score"] > main_line["score"]
    return choose


def higher_score(main_line: dict, korean_line: dict) -> bool:
    return korean_line["score"] > main_line["score"]


HANGUL = re.compile(r"[가-힣ᄀ-ᇿ㄰-㆏]")
KANA_KANJI = re.compile(r"[぀-ヿ一-鿿]")


def hangul_and_higher(main_line: dict, korean_line: dict) -> bool:
    """韓文模型的結果有韓文字母，而且分數較高。"""
    return bool(HANGUL.search(korean_line["text"])) and higher_score(main_line, korean_line)


def vote_by_image(main: dict, korean: dict, script: bool = False) -> dict:
    """整張截圖一起決定用哪個模型。

    script 為 False：超過一半的行韓文模型分數較高，就整張用韓文模型。
    script 為 True：比較「韓文模型分數較高、結果有韓文字母」和「主模型分數較高、結果有假名或漢字」
    的行數，前者較多就整張用韓文模型。
    """
    combined = {"images": []}
    korean_images = {image["image"]: image for image in korean["images"]}
    for image in main["images"]:
        other = korean_images[image["image"]]
        pairs = list(zip(image["lines"], other["lines"]))
        if script:
            korean_votes = sum(hangul_and_higher(a, b) for a, b in pairs)
            main_votes = sum(bool(KANA_KANJI.search(a["text"])) and not higher_score(a, b) for a, b in pairs)
        else:
            korean_votes = sum(higher_score(a, b) for a, b in pairs)
            main_votes = len(pairs) - korean_votes
        combined["images"].append(other if korean_votes > main_votes else image)
    return combined


# ---------------------------------------------------------------------------------------------
# 報告

def load_raw(device: str, combo: str, category: str) -> dict | None:
    path = RAW / device / combo / f"{category}.json"
    return json.loads(path.read_text(encoding="utf-8")) if path.exists() else None


def percent(value: float | None) -> str:
    return "—" if value is None else f"{value * 100:.1f}%"


def table(header: list[str], rows: list[list[str]]) -> list[str]:
    return ["| " + " | ".join(header) + " |", "|" + "---|" * len(header),
            *("| " + " | ".join(row) + " |" for row in rows)]


def report(categories: list[str], ground_truth: dict[str, list[gt.Page]]) -> str:
    out = ["# M0-11 OCR 評測（自動產生）", "",
           "由 `tools/eval/evaluate_ocr.py` 產生；指標的定義見該檔開頭。CER 是字元錯誤率（越低越好）。",
           ""]
    results: dict[tuple[str, str], Result] = {}
    for combo in MAIN_COMBOS + KOREAN_COMBOS:
        for category in categories:
            raw = load_raw(RAW_DEVICE.get(combo, "dml"), combo, category)
            if raw is not None:
                results[combo, category] = evaluate(category, ground_truth[category], raw)

    def cell(combo: str, category: str) -> str:
        result = results.get((combo, category))
        summary = Summary.of(main_records(result)) if result else None
        if summary is None:
            return "—"
        return f"{percent(summary.cer)}（漏 {percent(summary.missed)}）"

    out += ["## 1. 字元錯誤率（括號：漏掉的區塊）", "",
            "PP-OCRv6 不支援韓文、v6-tiny 也不支援日文，這些格子只是對照。"
            "Windows OCR 只有安裝了語言套件的語言才有結果。", ""]
    out += table(["組合", *categories],
                 [[combo, *(cell(combo, c) for c in categories)] for combo in MAIN_COMBOS + KOREAN_COMBOS])
    counts = [Summary.of(main_records(results[c2, c])) for c in categories
              for c2 in ("v6-medium",) if (c2, c) in results]
    out += ["", "評測區塊數：" + "、".join(f"{c} {s.blocks} 個（{s.characters} 字）"
                                     for c, s in zip(categories, counts) if s), ""]

    out += ["## 2. 完全正確的區塊比例", ""]
    out += table(["組合", *categories], [[combo, *(
        percent(Summary.of(main_records(results[combo, c])).exact) if (combo, c) in results else "—"
        for c in categories)] for combo in MAIN_COMBOS + KOREAN_COMBOS])

    out += ["", "## 3. 多出來的字（每張截圖平均，沒有落在任何區塊裡的辨識結果）", ""]
    out += table(["組合", *categories], [[combo, *(
        f"{results[combo, c].extra_chars / max(1, results[combo, c].pages):.1f}"
        if (combo, c) in results else "—" for c in categories)] for combo in MAIN_COMBOS])

    for device in ("dml", "cpu", "winrt"):
        rows = []
        for combo in MAIN_COMBOS + KOREAN_COMBOS:
            cells = []
            for category in categories:
                raw = load_raw(device, combo, category)
                if raw is None:
                    cells.append("—")
                    continue
                times = [i["timings_ms"]["detection"] + i["timings_ms"]["recognition"]
                         for i in raw["images"]]
                cells.append(f"{statistics.median(times):.0f}")
            if any(c != "—" for c in cells):
                rows.append([combo, *cells])
        if rows:
            name = {"dml": "DirectML", "cpu": "CPU", "winrt": "Windows OCR"}[device]
            out += ["", f"## 4. 耗時（{name}，每張截圖偵測＋辨識的中位數，毫秒）", "",
                    "截圖的大小各分類不同（遊戲是 4K 全螢幕），只能比較同一欄。", ""]
            out += table(["組合", *categories], rows)

    out += ["", "## 5. 依種類、方向、字級（v6-medium／v5-server；韓文分類用 ko-v6-medium-det／ko-v5-server-det）", ""]
    for category in categories:
        combos = ("ko-v6-medium-det", "ko-v5-server-det") if category.startswith("ko") \
            else ("v6-medium", "v5-server")
        groups: dict[str, dict[str, list[Record]]] = {}
        for combo in combos:
            if (combo, category) not in results:
                continue
            for record in main_records(results[combo, category]):
                for key in (f"種類：{record.kind}", f"方向：{record.direction}",
                            f"字級：{record.size}"):
                    groups.setdefault(key, {}).setdefault(combo, []).append(record)
        if not groups:
            continue
        out += [f"### {category}", ""]
        rows = []
        for key in sorted(groups):
            summaries = [Summary.of(groups[key].get(combo, [])) for combo in combos]
            blocks = next((s.blocks for s in summaries if s), 0)
            rows.append([key, str(blocks), *(percent(s.cer) if s else "—" for s in summaries)])
        out += table(["分組", "區塊", *(f"{c} CER" for c in combos)], rows) + [""]

    out += ["## 6. 語言判斷策略（design.md 4.4）", "",
            "主模型的分數低於門檻、而且韓文模型的分數比較高時，改用韓文模型的結果。"
            "「分數較高」是每一行都兩個模型都跑，取分數高的。"
            "「整張投票」也是兩個模型都跑，超過一半的行韓文模型分數較高時，整張截圖都用韓文模型。"
            "「韓文字（逐行）」是韓文模型的結果有韓文字母、而且分數較高時才換；"
            "「韓文字（整張）」是比較「韓文模型分數較高、結果有韓文字母」和「主模型分數較高、結果有假名或漢字」"
            "的行數，決定整張截圖用哪個模型。表中是 CER。", ""]
    for main, korean in KOREAN_PAIRS.items():
        strategies = [("只用主模型", None), ("只用韓文模型", "korean")]
        strategies += [(f"門檻 {t}", threshold_strategy(t)) for t in (0.5, 0.6, 0.7, 0.8, 0.9)]
        strategies += [("分數較高", higher_score), ("整張投票", "vote"),
                       ("韓文字（逐行）", hangul_and_higher), ("韓文字（整張）", "script-vote")]
        rows = []
        for name, choose in strategies:
            cells = []
            for category in categories:
                main_raw, korean_raw = load_raw("dml", main, category), load_raw("dml", korean, category)
                if main_raw is None or korean_raw is None:
                    cells.append("—")
                    continue
                if choose is None:
                    raw = main_raw
                elif choose == "korean":
                    raw = korean_raw
                elif choose in ("vote", "script-vote"):
                    raw = vote_by_image(main_raw, korean_raw, script=choose == "script-vote")
                else:
                    raw = combine_korean(main_raw, korean_raw, choose)
                summary = Summary.of(main_records(evaluate(category, ground_truth[category], raw)))
                cells.append(percent(summary.cer) if summary else "—")
            rows.append([name, *cells])
        out += [f"### 主模型 {main}＋{korean}", ""]
        out += table(["策略", *categories], rows) + [""]

    other = [(combo, r) for (combo, _), result in results.items() if combo == "v6-medium"
             for r in result.records if r.language != r.category.split("-")[0]]
    if other:
        summary = Summary.of([r for _, r in other])
        out += ["## 7. 和分類語言不同的區塊（v6-medium）", "",
                f"{summary.blocks} 個區塊，CER {percent(summary.cer)}，完全正確 {percent(summary.exact)}。",
                ""]

    out += ["## 8. 錯誤最多的區塊（v6-medium；韓文分類用 ko-v6-medium-det，每個分類前 12 個）", ""]
    for category in categories:
        combo = "ko-v6-medium-det" if category.startswith("ko") else "v6-medium"
        if (combo, category) not in results:
            continue
        worst = sorted(main_records(results[combo, category]),
                       key=lambda r: (r.distance, len(r.reference)), reverse=True)[:12]
        out += [f"### {category}", ""]
        out += table(["截圖", "區塊", "種類", "錯", "正確答案", "辨識結果"],
                     [[r.image[-10:], str(r.index), r.kind, str(r.distance),
                       r.reference[:40], r.hypothesis[:40] or "（沒有）"] for r in worst if r.distance])
        out += [""]
    return "\n".join(out) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--categories", nargs="+")
    args = parser.parse_args()
    categories = args.categories or sorted(
        p.name for p in PRIVATE.iterdir() if (p / "ground_truth.txt").exists())
    ground_truth = {c: gt.load(PRIVATE / c / "ground_truth.txt") for c in categories}
    text = report(categories, ground_truth)
    output = EVAL / "report.md"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8", newline="\n")
    print(f"報告：{output}")


if __name__ == "__main__":
    main()
