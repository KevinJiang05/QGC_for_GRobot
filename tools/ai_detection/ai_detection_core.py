#!/usr/bin/env python3
"""Shared latest-frame capture and inference scheduling for QGC AI bridges."""

from __future__ import annotations

import threading
import time
from collections.abc import Callable, Iterable
from typing import Any
from urllib.parse import urlsplit


def clean_source(source: str) -> str:
    cleaned = source.strip()
    while len(cleaned) >= 2 and cleaned[0] == cleaned[-1] and cleaned[0] in ("'", '"'):
        cleaned = cleaned[1:-1].strip()
    return cleaned


def parse_source(source: str) -> str | int:
    cleaned = clean_source(source)
    return int(cleaned) if cleaned.isdecimal() else cleaned


def source_description(source: str) -> str:
    """Return a credential-free source description suitable for logs."""
    cleaned = clean_source(source)
    parsed = urlsplit(cleaned)
    if parsed.scheme and parsed.hostname:
        try:
            parsed_port = parsed.port
        except ValueError:
            parsed_port = None
        port = f":{parsed_port}" if parsed_port else ""
        return f"{parsed.scheme}://{parsed.hostname}{port}"
    if cleaned.isdecimal():
        return f"camera {cleaned}"
    return "local media"


class LatestFrameReader:
    """Continuously decode one source while retaining only its newest frame."""

    def __init__(
        self,
        source_id: str,
        source: str,
        capture_factory: Callable[[str | int], Any],
        *,
        buffer_size_property: int | None = None,
        log: Callable[[str], None] = print,
    ) -> None:
        self.source_id = source_id
        self.source = source
        self._capture_factory = capture_factory
        self._buffer_size_property = buffer_size_property
        self._log = log
        self._stop_event = threading.Event()
        self._lock = threading.Lock()
        self._thread: threading.Thread | None = None
        self._capture: Any = None
        self._sequence = 0
        self._latest_frame: Any = None

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._stop_event.clear()
        self._thread = threading.Thread(
            target=self._run,
            name=f"ai-capture-{self.source_id}",
            daemon=True,
        )
        self._thread.start()

    def stop(self, timeout: float = 2.0) -> None:
        self._stop_event.set()
        with self._lock:
            capture = self._capture
        if capture is not None:
            try:
                capture.release()
            except Exception:
                pass
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=timeout)

    def latest_after(self, sequence: int) -> tuple[int, Any] | None:
        with self._lock:
            if self._sequence <= sequence or self._latest_frame is None:
                return None
            return self._sequence, self._latest_frame

    def _run(self) -> None:
        reconnect_delay = 0.5
        parsed_source = parse_source(self.source)

        while not self._stop_event.is_set():
            capture = None
            try:
                capture = self._capture_factory(parsed_source)
                with self._lock:
                    self._capture = capture

                if self._buffer_size_property is not None:
                    try:
                        capture.set(self._buffer_size_property, 1)
                    except Exception:
                        pass

                if not capture.isOpened():
                    raise RuntimeError("source open failed")

                reconnect_delay = 0.5
                self._log(f"{self.source_id} capture connected ({source_description(self.source)})")
                while not self._stop_event.is_set():
                    ok, frame = capture.read()
                    if not ok:
                        break
                    with self._lock:
                        self._sequence += 1
                        self._latest_frame = frame
            except Exception as exc:
                if not self._stop_event.is_set():
                    self._log(
                        f"{self.source_id} capture unavailable "
                        f"({source_description(self.source)}, {type(exc).__name__}); retrying"
                    )
            finally:
                if capture is not None:
                    try:
                        capture.release()
                    except Exception:
                        pass
                with self._lock:
                    if self._capture is capture:
                        self._capture = None

            if not self._stop_event.wait(reconnect_delay):
                reconnect_delay = min(reconnect_delay * 2.0, 10.0)


def collect_due_frames(
    readers: Iterable[LatestFrameReader],
    last_sequences: dict[str, int],
    next_due_times: dict[str, float],
    *,
    now: float | None = None,
    interval: float = 0.0,
) -> tuple[list[str], list[Any]]:
    """Collect at most one fresh frame per due source before model inference."""
    current_time = time.monotonic() if now is None else now
    source_ids: list[str] = []
    frames: list[Any] = []

    for reader in readers:
        source_id = reader.source_id
        if interval > 0 and current_time < next_due_times.get(source_id, 0.0):
            continue

        latest = reader.latest_after(last_sequences.get(source_id, 0))
        if latest is None:
            continue

        sequence, frame = latest
        last_sequences[source_id] = sequence
        next_due_times[source_id] = current_time + interval if interval > 0 else current_time
        source_ids.append(source_id)
        frames.append(frame)

    return source_ids, frames
