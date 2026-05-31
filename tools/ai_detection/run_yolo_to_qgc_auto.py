#!/usr/bin/env python3
"""Launch one YOLO bridge per DeepShark video source from QGC settings."""

from __future__ import annotations

import argparse
import configparser
import os
import subprocess
import sys
import time
from pathlib import Path


DEFAULT_SOURCES: list[tuple[str, str]] = []


def _clean_source(source: str) -> str:
    cleaned = source.strip()
    while len(cleaned) >= 2 and cleaned[0] == cleaned[-1] and cleaned[0] in ("'", '"'):
        cleaned = cleaned[1:-1].strip()
    return cleaned


def _candidate_settings_files(settings_file: str | None = None) -> list[Path]:
    appdata = Path(os.environ.get("APPDATA", ""))
    candidates = [
        appdata / "KevinJiang" / "QGC_KevinJiang Daily.ini",
        appdata / "KevinJiang" / "QGC_KevinJiang.ini",
        appdata / "QGroundControl" / "QGroundControl Daily.ini",
        appdata / "QGroundControl.org" / "QGroundControl.ini",
    ]
    if settings_file:
        candidates.insert(0, Path(settings_file))
    return candidates


def _read_sources(settings_file_arg: str | None) -> list[tuple[str, str]]:
    for settings_file in _candidate_settings_files(settings_file_arg):
        if not settings_file.exists():
            continue

        parser = configparser.ConfigParser()
        parser.optionxform = str
        parser.read(settings_file, encoding="utf-8")
        if not parser.has_section("DeepShark"):
            continue

        sources: list[tuple[str, str]] = []
        section = parser["DeepShark"]
        for index in range(1, 5):
            url = _clean_source(section.get(f"Video\\Camera{index}Url", ""))
            if url:
                sources.append((f"deepSharkVideo{index}", url))

        if sources:
            print(f"Loaded DeepShark video sources from {settings_file}", flush=True)
            return sources

    print("QGC DeepShark settings not found or no RTSP source configured.", flush=True)
    return DEFAULT_SOURCES


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Auto-launch YOLO bridges for DeepShark multi-video panel.")
    parser.add_argument("--model", required=True, help="YOLO model path.")
    parser.add_argument("--host", default="127.0.0.1", help="QGC host address.")
    parser.add_argument("--port", type=int, default=57610, help="QGC AI detection UDP port.")
    parser.add_argument("--imgsz", type=int, default=640, help="YOLO inference image size.")
    parser.add_argument("--conf", type=float, default=0.25, help="Confidence threshold.")
    parser.add_argument("--iou", type=float, default=0.45, help="NMS IoU threshold.")
    parser.add_argument("--device", default=None, help="Inference device, for example cpu, 0, cuda:0.")
    parser.add_argument("--max-fps", type=float, default=8.0, help="Maximum UDP send rate per source.")
    parser.add_argument("--log-every", type=float, default=2.0, help="Seconds between console status lines.")
    parser.add_argument("--settings-file", default=None, help="QGC settings ini file used to read DeepShark video sources.")
    parser.add_argument("--dry-run", action="store_true", help="Print detected sources without starting YOLO.")
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    bridge_script = Path(__file__).resolve().with_name("yolo_to_qgc_udp.py")
    sources = _read_sources(args.settings_file)

    if not sources:
        print("No RTSP source configured.", flush=True)
        return 1

    if args.dry_run:
        for source_id, source in sources:
            print(f"{source_id}: {source}", flush=True)
        return 0

    processes: list[subprocess.Popen[str]] = []
    for source_id, source in sources:
        command = [
            sys.executable,
            str(bridge_script),
            "--source",
            source,
            "--source-id",
            source_id,
            "--model",
            args.model,
            "--host",
            args.host,
            "--port",
            str(args.port),
            "--imgsz",
            str(args.imgsz),
            "--conf",
            str(args.conf),
            "--iou",
            str(args.iou),
            "--max-fps",
            str(args.max_fps),
            "--log-every",
            str(args.log_every),
        ]
        if args.device:
            command.extend(["--device", args.device])

        print(f"Starting {source_id}: {source}", flush=True)
        processes.append(subprocess.Popen(command, cwd=bridge_script.parent))

    try:
        while processes:
            alive: list[subprocess.Popen[str]] = []
            for process in processes:
                return_code = process.poll()
                if return_code is None:
                    alive.append(process)
                else:
                    print(f"YOLO bridge process exited with code {return_code}.", flush=True)
            processes = alive
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("Stopping YOLO bridge processes...", flush=True)
        for process in processes:
            process.terminate()
        for process in processes:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
