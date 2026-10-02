"""把 LaMa 的 ONNX 改寫成 DirectML 跑得動的版本（M4-01）。

LaMa 的傅立葉單元把 DFT 寫成「餘弦／正弦矩陣 × 5 維張量」的 MatMul（[N, N] × [批次, 通道, 高, 寬, 1]），
DirectML 的 MatMul 最多只吃 4 維，會回傳 E_INVALIDARG。批次固定是 1，所以在這些 MatMul 前面把
批次那一維拿掉（Squeeze）、後面再補回來（Unsqueeze），數學上完全等價。

    tools/eval/.venv/Scripts/python tools/eval/lama_for_directml.py \\
        --input models/lama/lama_fp32.onnx --output models/lama/lama_fp32_dml.onnx

需要 onnx 套件（pip install onnx）。
"""

from __future__ import annotations

import argparse
import sys

import onnx
from onnx import helper, shape_inference


def fix_batch(model: onnx.ModelProto) -> None:
    for value in list(model.graph.input) + list(model.graph.output):
        dim = value.type.tensor_type.shape.dim[0]
        dim.ClearField("dim_param")
        dim.dim_value = 1


def ranks(model: onnx.ModelProto) -> dict[str, int]:
    inferred = shape_inference.infer_shapes(model)
    out = {}
    for value in (list(inferred.graph.value_info) + list(inferred.graph.input)
                  + list(inferred.graph.output)):
        if value.type.tensor_type.HasField("shape"):
            out[value.name] = len(value.type.tensor_type.shape.dim)
    for initializer in model.graph.initializer:
        out[initializer.name] = len(initializer.dims)
    return out


def rewrite(model: onnx.ModelProto) -> int:
    rank = ranks(model)
    axes = helper.make_tensor("lama_dml_axis0", onnx.TensorProto.INT64, [1], [0])
    model.graph.initializer.append(axes)
    nodes = []
    changed = 0
    for node in model.graph.node:
        if node.op_type != "MatMul" or not any(rank.get(i) == 5 for i in node.input):
            nodes.append(node)
            continue
        inputs = []
        for name in node.input:
            if rank.get(name) == 5:
                squeezed = f"{name}__4d_{changed}"
                nodes.append(helper.make_node("Squeeze", [name, "lama_dml_axis0"], [squeezed],
                                              name=f"{node.name}/squeeze_{len(inputs)}"))
                inputs.append(squeezed)
            else:
                inputs.append(name)
        result = f"{node.output[0]}__4d"
        nodes.append(helper.make_node("MatMul", inputs, [result], name=node.name))
        nodes.append(helper.make_node("Unsqueeze", [result, "lama_dml_axis0"], [node.output[0]],
                                      name=f"{node.name}/unsqueeze"))
        changed += 1
    del model.graph.node[:]
    model.graph.node.extend(nodes)
    return changed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args(argv)
    model = onnx.load(args.input)
    fix_batch(model)
    changed = rewrite(model)
    onnx.checker.check_model(model)
    onnx.save(model, args.output)
    print(f"改寫了 {changed} 個 5 維的 MatMul → {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
