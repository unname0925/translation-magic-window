"""把整張截圖直接交給視覺 LLM 讀和翻，當作「看得見版面的人」的參考答案。

用途是找出我們的管線還差在哪裡：OCR 漏了什麼、段落該不該合併、譯文差多少。
視覺模型看得到整張圖，知道哪些字在同一個對話框裡，所以它的分段是很好的對照組。

    tools/eval/.venv/Scripts/python tools/eval/vision_reference.py \
        --images testdata/private/ja-manga/*.png --limit 3 \
        --ocr <ocr_cli 產生的 result.json> --output <報告.json>

金鑰只從環境變數 ANTHROPIC_API_KEY 讀取，不會寫進任何檔案。
文字很密的一頁可能會吃掉很多輸出 token（實測 3 頁中有 1 頁撞到 4000 的上限），
所以上限設 8000；解析不出來時會印出 stop_reason。

⚠️ 這支工具會把截圖上傳到 Anthropic 的 API。截圖有版權，平常一律只留在本機
（testdata/private 已經在 .gitignore 中）。只有在你明確要求比對時才執行它，
而且報告也只放在本機。
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

DEFAULT_MODEL = "claude-sonnet-5"
# 每百萬 token 的價格（美金），用來估算花費
PRICE_IN, PRICE_OUT = 3.0, 15.0

PROMPT = (
    "這是一張漫畫或遊戲畫面的截圖。請讀出上面所有的文字，並翻譯成台灣繁體中文。\n"
    "請以「一個對話框或一個段落」為單位分組，不要把同一句話拆開，也不要把不同的對話框合併。\n"
    "日文的振り仮名（ルビ）請用 {本文|讀音} 的形式標在本文裡，不要當成獨立的一段。\n"
    "只輸出 JSON 陣列，每個元素是 "
    '{"kind": "對白|旁白|擬聲詞|註|標題|介面|其他", "source": "原文", "translation": "譯文"}，'
    "依閱讀順序排列，不要加任何說明。"
)


def ask(image: Path, key: str, model: str, workspace: str) -> tuple[list[dict], int, int]:
    """送出一張圖，回傳（分組, 輸入 token, 輸出 token）。"""
    data = base64.b64encode(image.read_bytes()).decode("ascii")
    body = json.dumps({
        "model": model,
        "max_tokens": 8000,
        "messages": [{
            "role": "user",
            "content": [
                {"type": "image",
                 "source": {"type": "base64", "media_type": "image/png", "data": data}},
                {"type": "text", "text": PROMPT},
            ],
        }],
    }).encode("utf-8")
    headers = {"Content-Type": "application/json", "x-api-key": key,
               "anthropic-version": "2023-06-01"}
    if workspace:
        headers["anthropic-workspace-id"] = workspace

    for attempt in range(4):
        try:
            request = urllib.request.Request("https://api.anthropic.com/v1/messages",
                                             data=body, headers=headers)
            with urllib.request.urlopen(request, timeout=180) as response:
                answer = json.loads(response.read())
            break
        except urllib.error.HTTPError as error:
            if error.code not in (429, 500, 502, 503, 529) or attempt == 3:
                raise RuntimeError(f"HTTP {error.code}: "
                                   f"{error.read()[:200].decode('utf-8', 'replace')}") from error
            time.sleep(5 * (attempt + 1))
    text = "".join(part["text"] for part in answer["content"] if part["type"] == "text")
    usage = answer.get("usage", {})
    # 模型偶爾會在 JSON 前後加說明或用 ``` 包起來
    match = re.search(r"\[.*\]", text, re.S)
    try:
        groups = json.loads(match.group(0)) if match else []
    except json.JSONDecodeError:
        groups = []
    if not groups:
        # 解析不出來時把原因印出來，才看得出是被 max_tokens 截斷、拒答，還是格式不同
        print(f"   ⚠ {image.name} 解析不出分組（stop_reason="
              f"{answer.get('stop_reason')}, 輸出 {usage.get('output_tokens', 0)} token）："
              f"{text[:160]!r}")
    return groups, usage.get("input_tokens", 0), usage.get("output_tokens", 0)


def normalise(text: str) -> str:
    """比對用：去掉空白和 ルビ 標記，只留下本文。"""
    text = re.sub(r"\{([^|{}]*)\|[^{}]*\}", r"\1", text)
    return re.sub(r"\s+", "", text)


def compare(reference: list[dict], ours: list[dict]) -> dict:
    """我們的段落和參考答案對得上多少。用「字」為單位，不看分段方式。"""
    ref_text = "".join(normalise(g.get("source", "")) for g in reference)
    our_text = "".join(normalise(b.get("marked", b.get("text", ""))) for b in ours)
    ref_chars, our_chars = set(ref_text), set(our_text)
    covered = sum(1 for c in ref_text if c in our_chars)
    return {
        "參考分組數": len(reference),
        "我們的段落數": len(ours),
        "參考的字數": len(ref_text),
        "我們的字數": len(our_text),
        "我們讀到的比例": round(covered / len(ref_text), 3) if ref_text else 0.0,
        "我們多出來的字": len(our_chars - ref_chars),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--images", nargs="+", required=True)
    parser.add_argument("--limit", type=int, default=3, help="最多送幾張（省錢）")
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--ocr", help="ocr_cli 產生的 result.json，用來對照")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    key = os.environ.get("ANTHROPIC_API_KEY", "")
    if not key:
        print("沒有設定環境變數 ANTHROPIC_API_KEY", file=sys.stderr)
        return 2
    workspace = os.environ.get("ANTHROPIC_WORKSPACE_ID", "")

    ours_by_image: dict[str, list[dict]] = {}
    if args.ocr:
        for image in json.loads(Path(args.ocr).read_text(encoding="utf-8"))["images"]:
            ours_by_image[image["image"]] = image.get("blocks", [])

    report, tokens_in, tokens_out = [], 0, 0
    for path in [Path(p) for p in args.images][: args.limit]:
        groups, used_in, used_out = ask(path, key, args.model, workspace)
        tokens_in += used_in
        tokens_out += used_out
        entry = {"image": path.name, "reference": groups}
        if path.name in ours_by_image:
            entry["ours"] = ours_by_image[path.name]
            entry["comparison"] = compare(groups, ours_by_image[path.name])
        report.append(entry)
        print(f"{path.name}: 參考 {len(groups)} 組"
              + (f"、我們 {len(entry['ours'])} 段" if "ours" in entry else ""))

    cost = tokens_in / 1e6 * PRICE_IN + tokens_out / 1e6 * PRICE_OUT
    print(f"\n{len(report)} 張圖，{tokens_in} 輸入 + {tokens_out} 輸出 token，約 {cost:.3f} 美元")
    Path(args.output).write_text(
        json.dumps({"model": args.model, "cost_usd": round(cost, 4), "pages": report},
                   ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"報告：{args.output}（含截圖裡的文字，只放在本機）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
