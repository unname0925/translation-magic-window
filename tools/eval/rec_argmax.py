"""在 PP-OCR 辨識模型最後加上「取最大值」，另存成 inference_argmax.onnx（速度優化）。

原本的輸出是「每一行 × 每個時間點 × 每個字元」的機率表（8 行一批約 28 MB），整張要從顯示卡複製回來，
CTC 解碼其實只用到每個時間點最大的那個字元和它的機率。把 ArgMax／ReduceMax 放進模型，輸出只剩
[行數, 時間點, 2]（第 0 個是字元編號、第 1 個是機率），DirectML 上 8 行×寬 384 一批從 16.3 ms 降到 10.7 ms。

字元編號轉成 float 和機率放在同一個輸出裡，程式就能沿用只有一個 float 輸出的介面
（編號最大約 18710，float32 可以精確表示到 2^24）。ArgMax 相同最大值取第一個，和 np.argmax 一樣。

    tools/eval/.venv/Scripts/python tools/eval/rec_argmax.py models/PP-OCRv6_medium_rec models/PP-OCRv6_small_rec ...

需要 onnx 套件（pip install onnx）。
"""

from __future__ import annotations

import sys
from pathlib import Path

import onnx
from onnx import TensorProto, helper


def convert(model_dir: Path) -> Path:
    source = model_dir / "inference.onnx"
    model = onnx.load(source)
    if len(model.graph.output) != 1:
        raise SystemExit(f"{source}: 預期只有一個輸出")
    probabilities = model.graph.output[0].name
    opset = next(o.version for o in model.opset_import if o.domain in ("", "ai.onnx"))
    nodes = [helper.make_node("ArgMax", [probabilities], ["tmw_index"], axis=2, keepdims=1),
             helper.make_node("Cast", ["tmw_index"], ["tmw_index_float"], to=TensorProto.FLOAT)]
    if opset >= 18:
        model.graph.initializer.append(helper.make_tensor("tmw_axis", TensorProto.INT64, [1], [2]))
        nodes.append(helper.make_node("ReduceMax", [probabilities, "tmw_axis"], ["tmw_score"],
                                      keepdims=1))
    else:
        nodes.append(helper.make_node("ReduceMax", [probabilities], ["tmw_score"], axes=[2],
                                      keepdims=1))
    nodes.append(helper.make_node("Concat", ["tmw_index_float", "tmw_score"], ["tmw_decoded"],
                                  axis=2))
    model.graph.node.extend(nodes)
    del model.graph.output[:]
    model.graph.output.append(
        helper.make_tensor_value_info("tmw_decoded", TensorProto.FLOAT, ["batch", "time", 2]))
    onnx.checker.check_model(model)
    target = model_dir / "inference_argmax.onnx"
    onnx.save(model, target)
    return target


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__)
        return 2
    for folder in argv:
        print(convert(Path(folder)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
