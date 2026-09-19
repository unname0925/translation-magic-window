"""comic-text-detector（ONNX）的推論：找出漫畫裡的文字區塊。M0-11 評測偵測率用。

模型（models/comic-text-detector/comictextdetector.pt.onnx）：
- 輸入 images：1×3×1024×1024，RGB、0～1。原圖等比例縮放到長邊 1024，右邊和下面補 0。
- 輸出 blk：1×64512×7，YOLOv5 的候選框（中心 x、中心 y、寬、高、物件分數、2 個類別分數）。
- 輸出 seg：1×1×1024×1024，文字的遮罩（0～1）。
- 輸出 det：1×2×1024×1024，DB 的文字行機率圖和門檻圖（這裡沒有用到）。

後處理照 YOLOv5：分數＝物件分數×類別分數，門檻 0.4，NMS 的 IoU 0.35（comic-text-detector 的預設值）。
必須用 tools/eval/.venv-manga 的 Python 執行（需要 numpy、onnxruntime、Pillow）。
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parents[2]
MODEL = REPO_ROOT / "models" / "comic-text-detector" / "comictextdetector.pt.onnx"
INPUT_SIZE = 1024
CONFIDENCE = 0.4
NMS_IOU = 0.35


@dataclass
class Detection:
    box: tuple[int, int, int, int]  # 原圖座標
    score: float
    label: int


def letterbox(image: Image.Image) -> tuple[np.ndarray, float]:
    """等比例縮放到長邊 1024，右邊和下面補 0；回傳 1×3×1024×1024 和縮放比例。"""
    rgb = image.convert("RGB")
    scale = INPUT_SIZE / max(rgb.size)
    size = (max(1, round(rgb.width * scale)), max(1, round(rgb.height * scale)))
    canvas = np.zeros((INPUT_SIZE, INPUT_SIZE, 3), dtype=np.float32)
    canvas[:size[1], :size[0]] = np.asarray(rgb.resize(size, Image.Resampling.BILINEAR),
                                            dtype=np.float32) / 255.0
    return canvas.transpose(2, 0, 1)[None], scale


def iou(box: np.ndarray, boxes: np.ndarray) -> np.ndarray:
    x0 = np.maximum(box[0], boxes[:, 0])
    y0 = np.maximum(box[1], boxes[:, 1])
    x1 = np.minimum(box[2], boxes[:, 2])
    y1 = np.minimum(box[3], boxes[:, 3])
    inter = np.clip(x1 - x0, 0, None) * np.clip(y1 - y0, 0, None)
    area = (box[2] - box[0]) * (box[3] - box[1])
    areas = (boxes[:, 2] - boxes[:, 0]) * (boxes[:, 3] - boxes[:, 1])
    return inter / np.maximum(area + areas - inter, 1e-6)


def nms(boxes: np.ndarray, scores: np.ndarray, threshold: float) -> list[int]:
    order = list(np.argsort(-scores))
    keep = []
    while order:
        best = order.pop(0)
        keep.append(best)
        if order:
            overlaps = iou(boxes[best], boxes[order])
            order = [i for i, o in zip(order, overlaps) if o <= threshold]
    return keep


def postprocess(blk: np.ndarray, scale: float, size: tuple[int, int],
                confidence: float = CONFIDENCE) -> list[Detection]:
    candidates = blk[0]
    class_scores = candidates[:, 5:] * candidates[:, 4:5]
    labels = class_scores.argmax(axis=1)
    scores = class_scores.max(axis=1)
    mask = scores > confidence
    candidates, labels, scores = candidates[mask], labels[mask], scores[mask]
    boxes = np.stack([candidates[:, 0] - candidates[:, 2] / 2, candidates[:, 1] - candidates[:, 3] / 2,
                      candidates[:, 0] + candidates[:, 2] / 2, candidates[:, 1] + candidates[:, 3] / 2],
                     axis=1) / scale
    detections = []
    for i in nms(boxes, scores, NMS_IOU):
        x0, y0, x1, y1 = boxes[i]
        box = (max(0, int(x0)), max(0, int(y0)), min(size[0], int(round(x1))),
               min(size[1], int(round(y1))))
        if box[2] > box[0] and box[3] > box[1]:
            detections.append(Detection(box, float(scores[i]), int(labels[i])))
    return detections


class ComicTextDetector:
    def __init__(self, device: str = "dml"):
        import onnxruntime as ort

        options = ort.SessionOptions()
        if device == "dml":
            providers = ["DmlExecutionProvider"]
            options.enable_mem_pattern = False
            options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        else:
            providers = ["CPUExecutionProvider"]
        self.session = ort.InferenceSession(str(MODEL), options, providers=providers)

    def __call__(self, image: Image.Image) -> tuple[list[Detection], np.ndarray]:
        """回傳文字區塊，以及縮放回原圖大小的文字遮罩（0～1）。"""
        tensor, scale = letterbox(image)
        blk, seg, _ = self.session.run(None, {"images": tensor})
        detections = postprocess(blk, scale, image.size)
        width, height = round(image.width * scale), round(image.height * scale)
        mask = Image.fromarray((seg[0, 0, :height, :width] * 255).clip(0, 255).astype(np.uint8))
        mask = np.asarray(mask.resize(image.size, Image.Resampling.BILINEAR), dtype=np.float32) / 255
        return detections, mask
