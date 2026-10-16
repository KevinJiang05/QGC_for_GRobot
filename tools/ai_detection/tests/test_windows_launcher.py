from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


@unittest.skipUnless(sys.platform == "win32", "Exercises the Windows CMD wrapper")
class WindowsLauncherTest(unittest.TestCase):
    def _run_launcher(self, arguments: list[str], exit_code: int) -> tuple[int, dict]:
        launcher_source = Path(__file__).resolve().parents[1] / "run_yolo_to_qgc_udp.bat"
        with tempfile.TemporaryDirectory(prefix="qgc ai launcher ") as directory:
            root = Path(directory)
            tools = root / "tools" / "ai_detection"
            tools.mkdir(parents=True)
            output = root / "arguments.json"
            stub = (
                "import json, os, sys\n"
                "from pathlib import Path\n"
                "Path(os.environ['QGC_LAUNCHER_TEST_OUTPUT']).write_text(\n"
                "    json.dumps({'script': Path(__file__).name, 'arguments': sys.argv[1:]}), encoding='utf-8')\n"
                "sys.exit(int(os.environ['QGC_LAUNCHER_TEST_EXIT']))\n"
            )
            for name in ("run_yolo_to_qgc_auto.py", "yolo_to_qgc_udp.py"):
                (tools / name).write_text(stub, encoding="utf-8")
            launcher = tools / launcher_source.name
            text = launcher_source.read_text(encoding="utf-8")
            text = text.replace(
                'set "YOLO_PYTHON=D:\\Develop\\envs\\yolo\\Scripts\\python.exe"',
                f'set "YOLO_PYTHON={sys.executable}"',
            )
            launcher.write_text(text, encoding="utf-8")
            environment = os.environ.copy()
            environment["QGC_LAUNCHER_TEST_OUTPUT"] = str(output)
            environment["QGC_LAUNCHER_TEST_EXIT"] = str(exit_code)
            argument_text = " ".join(f'"{argument}"' for argument in arguments)
            command = (
                f'"{os.environ.get("COMSPEC", "cmd.exe")}" /d /s /c ""{launcher}" {argument_text}"'
            )
            result = subprocess.run(
                command, env=environment, capture_output=True, text=True, timeout=15, check=False
            )
            self.assertTrue(output.exists(), result.stdout + result.stderr)
            return result.returncode, json.loads(output.read_text(encoding="utf-8"))

    def test_single_source_forwards_only_remaining_arguments(self) -> None:
        source = "rtsp://127.0.0.1/live?stream=1&mode=2!"
        code, payload = self._run_launcher(
            [source, "--source-id", "camera one!&two", "--port", "57610"], 23
        )
        self.assertEqual(code, 23)
        self.assertEqual(payload["script"], "yolo_to_qgc_udp.py")
        arguments = payload["arguments"]
        self.assertEqual(arguments[:2], ["--source", source])
        self.assertEqual(arguments.count(source), 1)
        self.assertEqual(arguments[4:], ["--source-id", "camera one!&two", "--port", "57610"])

    def test_auto_branches_preserve_success_and_failure_exit_codes(self) -> None:
        for arguments in ([], ["--port", "57611", "--device", "cpu"]):
            for expected in (0, 37):
                with self.subTest(arguments=arguments, expected=expected):
                    code, payload = self._run_launcher(arguments, expected)
                    self.assertEqual(code, expected)
                    self.assertEqual(payload["script"], "run_yolo_to_qgc_auto.py")
                    self.assertEqual(payload["arguments"][2:], arguments)


if __name__ == "__main__":
    unittest.main()
