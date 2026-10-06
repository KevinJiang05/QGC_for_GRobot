from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

TOOLS_DIR = Path(__file__).resolve().parents[1]
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from ai_detection_core import clean_source, collect_due_frames, source_description  # noqa: E402
from run_yolo_to_qgc_auto import _open_instance_lock, _read_sources  # noqa: E402


class _FakeReader:
    def __init__(self, source_id: str, sequence: int, frame: object) -> None:
        self.source_id = source_id
        self.sequence = sequence
        self.frame = frame

    def latest_after(self, sequence: int) -> tuple[int, object] | None:
        if self.sequence <= sequence:
            return None
        return self.sequence, self.frame


class AIDetectionCoreTest(unittest.TestCase):
    def test_clean_source_removes_balanced_quotes(self) -> None:
        self.assertEqual(clean_source('  "rtsp://camera/live"  '), "rtsp://camera/live")

    def test_source_description_never_logs_credentials_or_path_tokens(self) -> None:
        description = source_description("rtsp://user:password@example.test:8554/live?token=secret")
        self.assertEqual(description, "rtsp://example.test:8554")
        self.assertNotIn("password", description)
        self.assertNotIn("secret", description)

    def test_collect_due_frames_limits_before_inference(self) -> None:
        reader = _FakeReader("camera1", 1, "frame-1")
        last_sequences: dict[str, int] = {}
        next_due_times: dict[str, float] = {}

        source_ids, frames = collect_due_frames(
            [reader], last_sequences, next_due_times, now=10.0, interval=0.5
        )
        self.assertEqual(source_ids, ["camera1"])
        self.assertEqual(frames, ["frame-1"])

        reader.sequence = 2
        reader.frame = "frame-2"
        source_ids, frames = collect_due_frames(
            [reader], last_sequences, next_due_times, now=10.1, interval=0.5
        )
        self.assertEqual(source_ids, [])
        self.assertEqual(frames, [])
        self.assertEqual(last_sequences["camera1"], 1)

        source_ids, frames = collect_due_frames(
            [reader], last_sequences, next_due_times, now=10.5, interval=0.5
        )
        self.assertEqual(source_ids, ["camera1"])
        self.assertEqual(frames, ["frame-2"])

    def test_settings_sources_are_loaded_in_panel_order(self) -> None:
        settings_file = Path(__file__).resolve().parent / "fixtures" / "deepshark_settings.ini"
        sources = _read_sources(str(settings_file))
        self.assertEqual(
            sources,
            [
                ("deepSharkVideo1", "rtsp://user:password@192.0.2.10:8554/camera1"),
                ("deepSharkVideo2", "rtsp://192.0.2.11:8554/camera2"),
            ],
        )

    def test_instance_lock_rejects_second_auto_bridge(self) -> None:
        first = _open_instance_lock(0)
        self.addCleanup(first.close)
        port = first.getsockname()[1]
        with self.assertRaises(OSError):
            second = _open_instance_lock(port)
            second.close()

    def test_auto_sources_prefer_debug_settings(self) -> None:
        with (
            tempfile.TemporaryDirectory() as directory,
            patch.dict("os.environ", {"APPDATA": directory}),
        ):
            root = Path(directory) / "KevinJiang"
            root.mkdir()
            (root / "QGC_KevinJiang Daily.ini").write_text(
                "[DeepShark]\nVideo\\Camera1Url=rtsp://old.test/live\n", encoding="utf-8"
            )
            debug = root / "QGC_KevinJiang_v5_1_5_Debug Daily.ini"
            debug.write_text(
                "[DeepShark]\nVideo\\Camera1Url=rtsp://debug.test/live\n", encoding="utf-8"
            )
            self.assertEqual(_read_sources(None), [("deepSharkVideo1", "rtsp://debug.test/live")])
            debug.write_text("[DeepShark]\n", encoding="utf-8")
            self.assertEqual(_read_sources(None), [])

    def test_explicit_settings_do_not_fall_back_to_another_application(self) -> None:
        with (
            tempfile.TemporaryDirectory() as directory,
            patch.dict("os.environ", {"APPDATA": directory}),
        ):
            root = Path(directory) / "KevinJiang"
            root.mkdir()
            (root / "QGC_KevinJiang Daily.ini").write_text(
                "[DeepShark]\nVideo\\Camera1Url=rtsp://old.test/live\n", encoding="utf-8"
            )
            explicit = Path(directory) / "selected.ini"
            self.assertEqual(_read_sources(str(explicit)), [])
            explicit.write_text("[DeepShark]\n", encoding="utf-8")
            self.assertEqual(_read_sources(str(explicit)), [])


if __name__ == "__main__":
    unittest.main()
