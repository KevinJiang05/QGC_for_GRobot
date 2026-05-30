#!/usr/bin/env python3
"""Send moving sample detections to QGC for overlay testing."""

from __future__ import annotations

import argparse
import json
import math
import socket
import time


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send sample AI detections to QGC over UDP.")
    parser.add_argument("--host", default="127.0.0.1", help="QGC host address.")
    parser.add_argument("--port", type=int, default=57610, help="QGC AI detection UDP port.")
    parser.add_argument("--fps", type=float, default=10.0, help="Send rate.")
    parser.add_argument("--source-id", default="", help="Optional source id, for example deepSharkVideo4.")
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    interval = 1.0 / max(args.fps, 0.1)
    destination = (args.host, args.port)
    start = time.time()

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_socket:
        while True:
            t = time.time() - start
            x = 0.40 + 0.22 * math.sin(t * 0.8)
            y = 0.28 + 0.12 * math.cos(t * 0.6)
            payload = {
                "timestamp": time.time(),
                "source_id": args.source_id,
                "frame_width": 1280,
                "frame_height": 720,
                "detections": [
                    {
                        "label": "sample",
                        "confidence": 0.91,
                        "x": x,
                        "y": y,
                        "w": 0.18,
                        "h": 0.22,
                    }
                ],
            }
            udp_socket.sendto(json.dumps(payload, separators=(",", ":")).encode("utf-8"), destination)
            print(f"sent sample detection to {args.host}:{args.port}", flush=True)
            time.sleep(interval)


if __name__ == "__main__":
    raise SystemExit(main())
