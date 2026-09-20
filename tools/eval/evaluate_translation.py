"""M0-12：翻譯評測的報告。把盲評的分數對照回引擎，加上自動檢查的項目。

    tools/eval/.venv/Scripts/python tools/eval/evaluate_translation.py

- 盲評分數（rate_translations.py）：各引擎的平均分，依分類、依種類分開。
- 自動檢查（不需要人看）：
  - 失敗：引擎回報錯誤或譯文是空的（數量對不上、HTTP 錯誤等）。
  - 沒翻：譯文裡還留著來源語言的字（假名、韓文字母；英文來源不算）。
  - 簡體字：OpenCC 轉換後還有簡體字（轉換表沒蓋到的字）。
  - ルビ標記：原文有 `{本文|讀音}` 時，譯文是否保留同樣數量的標記。
  - 耗時：每段的中位數（Google 和 Gemini 是一頁一次，會平均到每段）。

報告寫在 build/translation_eval/m0-12/report.md（含有截圖裡的文字，不進版本控制）。
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
from pathlib import Path

from evaluate_ocr import table
from rate_translations import RATINGS, load_engines
from translate import CATEGORIES, OUTPUT

KANA = re.compile(r"[぀-ヿ]")
HANGUL = re.compile(r"[가-힣]")
MARKER = re.compile(r"\{[^{}|]+\|[^{}|]+\}")
# OpenCC s2twp 之後還出現這些字，表示沒有轉成繁體（挑常見、簡繁不同的字）
SIMPLIFIED = "为这个说时过还们来对开关门问间东车马长鱼鸟见贝页风飞习书国图学实动务头处务汉语译"


def source_language(key: str) -> str:
    return key.split("-")[0]


def leftover_source(key: str, translation: str) -> bool:
    """譯文裡還留著來源語言的字（英文來源沒辦法這樣判斷）。"""
    language = source_language(key)
    if language == "ja":
        return bool(KANA.search(translation))
    if language == "ko":
        return bool(HANGUL.search(translation))
    return False


def checks(rows: dict[str, dict]) -> dict[str, float | int]:
    """一個引擎的自動檢查結果。"""
    failed = {k for k, r in rows.items() if r["error"] or not r["translation"].strip()}
    done = [r for k, r in rows.items() if k not in failed]
    marked = [r for r in done if MARKER.search(r["source"])]
    kept = [r for r in marked if len(MARKER.findall(r["translation"])) == len(MARKER.findall(r["source"]))]
    return {
        "段數": len(rows),
        "失敗": len(failed),
        "沒翻": sum(leftover_source(r["key"], r["translation"]) for r in done),
        "簡體字": sum(any(c in SIMPLIFIED for c in r["translation"]) for r in done),
        "ルビ標記": f"{len(kept)}/{len(marked)}" if marked else "—",
        "每段毫秒": round(statistics.median([r["ms"] for r in done]), 1) if done else 0,
    }


def ratings_by_engine(path: Path) -> dict[str, dict[str, list[int]]]:
    """引擎 → 分組 → 分數。分組是「全部」、分類和種類。"""
    if not path.exists():
        return {}
    items = json.loads(path.read_text(encoding="utf-8"))["items"]
    scores: dict[str, dict[str, list[int]]] = {}
    for item in items:
        for engine, score in item["scores"].items():
            groups = scores.setdefault(engine, {})
            for group in ("全部", item["category"], f"語言：{item['category'].split('-')[0]}"):
                groups.setdefault(group, []).append(score)
    return scores


def average(values: list[int] | None) -> str:
    return "—" if not values else f"{statistics.mean(values):.2f}"


def report(engines: dict[str, dict[str, dict]], scores: dict[str, dict[str, list[int]]]) -> str:
    names = sorted(engines)
    models = {}
    for path in sorted(OUTPUT.glob("*.json")):
        if path.name != RATINGS.name:
            data = json.loads(path.read_text(encoding="utf-8"))
            models[data["engine"]] = data.get("model", "")
    out = ["# M0-12 翻譯評測（自動產生）", "",
           "由 `tools/eval/evaluate_translation.py` 產生；盲評的分數來自 `rate_translations.py`。", ""]
    out += table(["引擎", "模型"], [[name, models.get(name, "")] for name in names]) + [""]

    out += ["## 1. 盲評分數（1 完全錯、2 意思有誤、3 意思對但不自然、4 好、5 很好）", ""]
    groups = ["全部", *(f"語言：{language}" for language in ("ja", "en", "ko")), *CATEGORIES]
    available = [g for g in groups if any(g in scores.get(n, {}) for n in names)]
    if available:
        out += table(["分組", "段數", *names],
                     [[group, str(max(len(scores.get(n, {}).get(group, [])) for n in names)),
                       *(average(scores.get(n, {}).get(group)) for n in names)]
                      for group in available])
    else:
        out += ["（還沒有盲評分數，先執行 `rate_translations.py`）"]
    out += [""]

    out += ["## 2. 自動檢查", "",
            "「沒翻」是譯文裡還留著假名或韓文字母；「簡體字」是 OpenCC 轉換後仍有簡體字；"
            "「ルビ標記」是有 `{本文|讀音}` 的段落中，譯文保留標記的比例。", ""]
    rows = {name: checks(engines[name]) for name in names}
    columns = list(next(iter(rows.values())))
    out += table(["引擎", *columns], [[name, *(str(rows[name][c]) for c in columns)]
                                      for name in names])
    out += [""]

    out += ["## 3. 各引擎最低分的段落", ""]
    if RATINGS.exists():
        items = json.loads(RATINGS.read_text(encoding="utf-8"))["items"]
        for name in names:
            worst = sorted((i for i in items if name in i["scores"]),
                           key=lambda i: i["scores"][name])[:8]
            if not worst:
                continue
            out += [f"### {name}", ""]
            out += table(["分數", "分類", "原文", "譯文", "備註"],
                         [[str(i["scores"][name]), i["category"], i["source"][:40],
                           engines[name][i["key"]]["translation"][:40], i.get("note", "")]
                          for i in worst]) + [""]
    return "\n".join(out) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--engines", nargs="+")
    args = parser.parse_args()

    engines = load_engines(args.engines)
    if not engines:
        raise SystemExit(f"{OUTPUT} 裡沒有翻譯結果，先跑 translate.py")
    path = OUTPUT / "report.md"
    path.write_text(report(engines, ratings_by_engine(RATINGS)), encoding="utf-8", newline="\n")
    print(f"報告：{path}")


if __name__ == "__main__":
    main()
