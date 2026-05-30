#!/usr/bin/env python3
"""Run YOLO detection on a video source and send boxes to QGC over UDP.

The QGC side listens on UDP 57610 and expects JSON with normalized boxes or
pixel-space bbox values. This script sends both, so the receiver can stay simple.
"""

from __future__ import annotations

import argparse
import json
import socket
import time
from typing import Any


def _parse_source(source: str) -> str | int:
    return int(source) if source.isdecimal() else source


def _clamp_unit(value: float) -> float:
    return max(0.0, min(1.0, value))


def _build_payload(result: Any, source_id: str) -> dict[str, Any]:
    frame_height, frame_width = result.orig_shape
    names = result.names
    detections: list[dict[str, Any]] = []

    if result.boxes is not None:
        for box in result.boxes:
            left, top, right, bottom = [float(v) for v in box.xyxy[0].tolist()]
            confidence = float(box.conf[0])
            class_id = int(box.cls[0])
            label = names.get(class_id, str(class_id)) if isinstance(names, dict) else str(class_id)

            width = max(0.0, right - left)
            height = max(0.0, bottom - top)
            detections.append(
                {
                    "class_id": class_id,
                    "label": label,
                    "confidence": _clamp_unit(confidence),
                    "bbox": [left, top, right, bottom],
                    "x": _clamp_unit(left / frame_width),
                    "y": _clamp_unit(top / frame_height),
                    "w": _clamp_unit(width / frame_width),
                    "h": _clamp_unit(height / frame_height),
                }
            )

    return {
        "timestamp": time.time(),
        "source_id": source_id,
        "frame_width": int(frame_width),
        "frame_height": int(frame_height),
        "detections": detections,
    }


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send YOLO detections to QGC video overlay over UDP.")
    parser.add_argument("--source", required=True, help="RTSP URL, video path, image path, or camera index such as 0.")
    parser.add_argument("--source-id", default="", help="Optional source id used by QGC to route boxes to a video panel.")
    parser.add_argument("--model", default="yolov8n.pt", help="YOLO model path or Ultralytics model name.")
    parser.add_argument("--host", default="127.0.0.1", help="QGC host address.")
    parser.add_argument("--port", type=int, default=57610, help="QGC AI detection UDP port.")
    parser.add_argument("--imgsz", type=int, default=640, help="YOLO inference image size.")
    parser.add_argument("--conf", type=float, default=0.25, help="Confidence threshold.")
    parser.add_argument("--iou", type=float, default=0.45, help="NMS IoU threshold.")
    parser.add_argument("--device", default=None, help="Inference device, for example cpu, 0, cuda:0.")
    parser.add_argument("--classes", type=int, nargs="*", default=None, help="Optional class IDs to keep.")
    parser.add_argument("--max-fps", type=float, default=15.0, help="Maximum UDP send rate. Set 0 to send every result.")
    parser.add_argument("--log-every", type=float, default=2.0, help="Seconds between console status lines.")
    return parser.parse_args()


def main() -> int:
    args = _parse_args()

    try:
        from ultralytics import YOLO
    except ImportError as exc:
        raise SystemExit(
            "Missing dependency: ultralytics. Install with `pip install -r tools/ai_detection/requirements.txt`."
        ) from exc

    model = YOLO(args.model)
    destination = (args.host, args.port)
    interval = 1.0 / args.max_fps if args.max_fps > 0 else 0.0
    last_sent = 0.0
    last_log = 0.0

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_socket:
        results = model.predict(
            source=_parse_source(args.source),
            stream=True,
            imgsz=args.imgsz,
            conf=args.conf,
            iou=args.iou,
            device=args.device,
            classes=args.classes,
            verbose=False,
        )

        for result in results:
            now = time.time()
            if interval > 0 and now - last_sent < interval:
                continue

            payload = _build_payload(result, args.source_id)
            message = json.dumps(payload, separators=(",", ":")).encode("utf-8")
            udp_socket.sendto(message, destination)
            last_sent = now

            if args.log_every > 0 and now - last_log >= args.log_every:
                print(
                    f"{args.source_id or 'default'} sent {len(payload['detections'])} detections "
                    f"({payload['frame_width']}x{payload['frame_height']}) to {args.host}:{args.port}",
                    flush=True,
                )
                last_log = now

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
