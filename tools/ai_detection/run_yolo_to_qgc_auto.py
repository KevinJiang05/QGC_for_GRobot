#!/usr/bin/env python3
"""Run one YOLO model for all configured DeepShark video sources."""

from __future__ import annotations

import argparse
import configparser
import json
import os
import signal
import socket
import threading
import time
from pathlib import Path
from typing import Any

from ai_detection_core import (
    LatestFrameReader,
    clean_source,
    collect_due_frames,
    source_description,
)
from yolo_to_qgc_udp import _build_payload

DEFAULT_SOURCES: list[tuple[str, str]] = []


def _candidate_settings_files(settings_file: str | None = None) -> list[Path]:
    if settings_file:
        return [Path(settings_file)]
    appdata = Path(os.environ.get("APPDATA", ""))
    candidates = [
        appdata / "KevinJiang" / "QGC_KevinJiang_v5_1_5_Debug Daily.ini",
        appdata / "KevinJiang" / "QGC_KevinJiang_v5_1_5_Debug.ini",
        appdata / "KevinJiang" / "QGC_KevinJiang Daily.ini",
        appdata / "KevinJiang" / "QGC_KevinJiang.ini",
        appdata / "QGroundControl" / "QGroundControl Daily.ini",
        appdata / "QGroundControl.org" / "QGroundControl.ini",
    ]
    return candidates


def _read_sources(settings_file_arg: str | None) -> list[tuple[str, str]]:
    for settings_file in _candidate_settings_files(settings_file_arg):
        if not settings_file.exists():
            continue

        parser = configparser.ConfigParser()
        parser.optionxform = str
        parser.read(settings_file, encoding="utf-8")
        if not parser.has_section("DeepShark"):
            break

        sources: list[tuple[str, str]] = []
        section = parser["DeepShark"]
        for index in range(1, 5):
            url = clean_source(section.get(f"Video\\Camera{index}Url", ""))
            if url:
                sources.append((f"deepSharkVideo{index}", url))

        if sources:
            print(f"Loaded DeepShark video sources from {settings_file}", flush=True)
            return sources
        break

    print("QGC DeepShark settings not found or no RTSP source configured.", flush=True)
    return DEFAULT_SOURCES


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Auto-launch YOLO bridges for DeepShark multi-video panel."
    )
    parser.add_argument("--model", required=True, help="YOLO model path.")
    parser.add_argument("--host", default="127.0.0.1", help="QGC host address.")
    parser.add_argument("--port", type=int, default=57610, help="QGC AI detection UDP port.")
    parser.add_argument("--imgsz", type=int, default=640, help="YOLO inference image size.")
    parser.add_argument("--conf", type=float, default=0.25, help="Confidence threshold.")
    parser.add_argument("--iou", type=float, default=0.45, help="NMS IoU threshold.")
    parser.add_argument(
        "--device", default=None, help="Inference device, for example cpu, 0, cuda:0."
    )
    parser.add_argument(
        "--max-fps",
        type=float,
        default=8.0,
        help="Maximum inference rate per source. Set 0 for every fresh frame.",
    )
    parser.add_argument(
        "--log-every", type=float, default=2.0, help="Seconds between console status lines."
    )
    parser.add_argument(
        "--settings-file",
        default=None,
        help="QGC settings ini file used to read DeepShark video sources.",
    )
    parser.add_argument(
        "--instance-port",
        type=int,
        default=0,
        help="Local single-instance lock port. Defaults to UDP port + 1.",
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="Print detected sources without starting YOLO."
    )
    return parser.parse_args()


def _open_instance_lock(port: int) -> socket.socket:
    lock_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            lock_socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        lock_socket.bind(("127.0.0.1", port))
        lock_socket.listen(1)
        return lock_socket
    except Exception:
        lock_socket.close()
        raise


def _install_stop_handlers(stop_event: threading.Event) -> None:
    def request_stop(_signum: int, _frame: Any) -> None:
        stop_event.set()

    signal.signal(signal.SIGINT, request_stop)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, request_stop)


def _run_detection(args: argparse.Namespace, sources: list[tuple[str, str]]) -> int:
    try:
        import cv2

        from ultralytics import YOLO
    except ImportError as exc:
        raise SystemExit(
            "Missing AI dependency. Install with `pip install -r tools/ai_detection/requirements.txt`."
        ) from exc

    lock_port = (
        args.instance_port
        if args.instance_port > 0
        else (args.port + 1 if args.port < 65535 else args.port - 1)
    )
    try:
        instance_lock = _open_instance_lock(lock_port)
    except OSError as exc:
        print(
            f"AI detection is already running or lock port {lock_port} is unavailable: {exc}",
            flush=True,
        )
        return 2

    stop_event = threading.Event()
    _install_stop_handlers(stop_event)
    model = YOLO(args.model)
    readers = [
        LatestFrameReader(
            source_id,
            source,
            cv2.VideoCapture,
            buffer_size_property=cv2.CAP_PROP_BUFFERSIZE,
            log=lambda message: print(message, flush=True),
        )
        for source_id, source in sources
    ]
    destination = (args.host, args.port)
    interval = 1.0 / args.max_fps if args.max_fps > 0 else 0.0
    last_sequences: dict[str, int] = {}
    next_due_times: dict[str, float] = {}
    last_log = 0.0

    print(f"Loaded one YOLO model for {len(readers)} source(s).", flush=True)
    for reader in readers:
        reader.start()

    try:
        with instance_lock, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_socket:
            while not stop_event.is_set():
                source_ids, frames = collect_due_frames(
                    readers,
                    last_sequences,
                    next_due_times,
                    interval=interval,
                )
                if not frames:
                    stop_event.wait(0.01)
                    continue

                results = model.predict(
                    source=frames,
                    stream=False,
                    imgsz=args.imgsz,
                    conf=args.conf,
                    iou=args.iou,
                    device=args.device,
                    verbose=False,
                )
                now = time.monotonic()
                detection_count = 0
                for source_id, result in zip(source_ids, results, strict=False):
                    payload = _build_payload(result, source_id)
                    message = json.dumps(payload, separators=(",", ":")).encode("utf-8")
                    udp_socket.sendto(message, destination)
                    detection_count += len(payload["detections"])

                if args.log_every > 0 and now - last_log >= args.log_every:
                    print(
                        f"Inferred {len(frames)} source(s), sent {detection_count} detections "
                        f"to {args.host}:{args.port}",
                        flush=True,
                    )
                    last_log = now
    except KeyboardInterrupt:
        stop_event.set()
    finally:
        stop_event.set()
        for reader in readers:
            reader.stop()
        print("AI detection stopped; all capture threads released.", flush=True)

    return 0


def main() -> int:
    args = _parse_args()
    sources = _read_sources(args.settings_file)

    if not sources:
        print("No RTSP source configured.", flush=True)
        return 1

    if args.dry_run:
        for source_id, source in sources:
            print(f"{source_id}: {source_description(source)}", flush=True)
        return 0

    return _run_detection(args, sources)


if __name__ == "__main__":
    raise SystemExit(main())
