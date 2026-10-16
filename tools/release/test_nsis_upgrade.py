"""Run the production upgrade prelude against workspace-only NSIS fixtures."""

from __future__ import annotations

import os
import re
import subprocess
import unittest
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "deploy/windows/nullsoft_installer.nsi"


class InstallerUpgradeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        candidates = [
            ROOT / "build-release/nsis-portable/nsis-3.11/makensis.exe",
            ROOT / "build-v5.1.5-debug/nsis-portable/nsis-3.11/makensis.exe",
            Path("C:/Program Files (x86)/NSIS/makensis.exe"),
        ]
        cls.compiler = next((path for path in candidates if path.is_file()), None)
        if cls.compiler is None or os.name != "nt":
            raise unittest.SkipTest("Existing Windows NSIS compiler required")
        cls.run_root = ROOT / ".tmp/codex/installer-upgrade-tests" / uuid.uuid4().hex
        cls.run_root.mkdir(parents=True)
        cls.source = SCRIPT.read_text(encoding="utf-8")

    def _run(self, command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
        self.assertTrue(cwd.resolve().is_relative_to(self.run_root.resolve()))
        temporary = cwd / "runtime-temp"
        temporary.mkdir(exist_ok=True)
        environment = os.environ.copy()
        environment["TEMP"] = str(temporary)
        environment["TMP"] = str(temporary)
        return subprocess.run(
            command,
            cwd=cwd,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=60,
            creationflags=subprocess.CREATE_NO_WINDOW,
            env=environment,
            check=False,
        )

    def _compile(self, script: Path) -> None:
        result = self._run([str(self.compiler), "/V2", str(script)], script.parent)
        (script.with_suffix(".compile.log")).write_text(
            result.stdout + result.stderr, encoding="utf-8"
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def _case(
        self,
        name: str,
        *,
        quoted: bool = True,
        same: bool = False,
        fail: bool = False,
        missing: bool = False,
        nested: bool = False,
        new_exists: bool = True,
        prefix_sibling: bool = False,
        normalized_nested: bool = False,
        trailing_separator: bool = False,
        invalid_quotes: bool = False,
        invalid_executable: bool = False,
    ) -> None:
        case = (self.run_root / name).resolve()
        self.assertTrue(
            case.is_relative_to((ROOT / ".tmp/codex/installer-upgrade-tests").resolve())
        )
        old = case / "Old Version"
        new = old if same else old / "New Version" if nested else case / "New Version"
        if prefix_sibling:
            new = case / "Old Version Copy"
        old.mkdir(parents=True)
        if new_exists:
            new.mkdir(exist_ok=True)
        (old / "sentinel.txt").write_text("old payload", encoding="utf-8")
        if not same and new_exists:
            (new / "sentinel.txt").write_text("keep new destination", encoding="utf-8")
        data = case / "user-data.txt"
        data.write_text("keep settings", encoding="utf-8")
        old_exe = old / "Fixture-Uninstall.exe"
        bootstrap = case / "bootstrap.nsi"
        bootstrap.write_text(
            f'''
!include "LogicLib.nsh"
!include "FileFunc.nsh"
Name "Upgrade fixture"
OutFile "{case / "bootstrap.exe"}"
RequestExecutionLevel user
SilentInstall silent
SilentUnInstall silent
Section
    WriteUninstaller "{old_exe}"
SectionEnd
Section "Uninstall"
    ; Never remove anything outside these two verified workspace fixture directories.
    StrCmp $INSTDIR "{old}" allowed
    StrCmp $INSTDIR "{new}" allowed
    SetErrorLevel 99
    Quit
allowed:
    FileOpen $9 "{case / "observed-directory.txt"}" w
    FileWrite $9 "$INSTDIR"
    FileClose $9
    ${{GetParameters}} $R0
    ${{GetOptions}} $R0 "-LEAVE_DATA=" $R1
    ${{If}} $R1 != 1
        Delete "{data}"
    ${{EndIf}}
    ${{If}} {int(fail)} == 1
        SetErrorLevel 7
        Quit
    ${{EndIf}}
    Delete "$INSTDIR\\sentinel.txt"
SectionEnd
''',
            encoding="utf-8",
        )
        self._compile(bootstrap)
        result = self._run([str(case / "bootstrap.exe"), "/S"], case)
        self.assertEqual(result.returncode, 0)
        self.assertTrue(old_exe.is_file())
        if missing:
            # Remove only the exact fixture file, never a directory or a computed external path.
            self.assertTrue(old_exe.resolve().is_relative_to(case))
            old_exe.unlink()
        if invalid_executable:
            self.assertTrue(old_exe.resolve().is_relative_to(case))
            old_exe.write_bytes(b"This exact workspace fixture is not an executable.\r\n")

        prelude = self.source.split('Section "Install" SecMain\n', 1)[1].split("doInstall:\n", 1)[0]
        command = f'"{old_exe}"' if quoted else str(old_exe)
        if invalid_quotes:
            command = f'"{old_exe}'
        prelude = re.sub(r"(?m)^\s*ReadRegStr .*", lambda _: f"    StrCpy $R0 '{command}'", prelude)
        prelude = re.sub(
            r"(?m)^\s*DeleteRegKey .*", '    DetailPrint "fixture registry cleanup"', prelude
        )

        # Preserve each production failure message as fixture evidence instead of opening a UI.
        def record_message(match: re.Match[str]) -> str:
            return (
                f'    FileOpen $9 "{case / "failure-message.txt"}" a\n'
                f"    FileWrite $9 {match.group(1)}\n"
                "    FileClose $9"
            )

        prelude = re.sub(r'(?m)^\s*MessageBox\s+\S+\s+(".*")$', record_message, prelude)
        self.assertNotRegex(prelude, r"(?im)^\s*(?:ReadReg|WriteReg|DeleteReg)")
        self.assertNotRegex(prelude, r"(?im)^\s*(?:CreateShortCut|RMDir|MessageBox)\b")
        install_target = str(new)
        if normalized_nested:
            install_target = str(old / "intermediate" / ".." / "New Version")
        if trailing_separator:
            install_target += "\\"
        upgrade = case / "upgrade.nsi"
        upgrade.write_text(
            f'''
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!define APPNAME "Fixture"
!define EXENAME "Fixture"
Name "Upgrade fixture"
OutFile "{case / "upgrade.exe"}"
InstallDir "{install_target}"
RequestExecutionLevel user
SilentInstall silent
Section
{prelude}
doInstall:
    FileOpen $9 "{case / "installed-directory.txt"}" w
    FileWrite $9 "$INSTDIR"
    FileClose $9
SectionEnd
''',
            encoding="utf-8",
        )
        self._compile(upgrade)
        result = self._run([str(case / "upgrade.exe"), "/S"], case)
        aborted = fail or nested or invalid_quotes or invalid_executable
        if aborted:
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((case / "installed-directory.txt").exists())
            self.assertTrue((old / "sentinel.txt").exists())
            if new_exists:
                self.assertTrue((new / "sentinel.txt").exists())
            if nested:
                self.assertFalse((case / "observed-directory.txt").exists())
                self.assertIn(
                    "inside the previous installation directory",
                    (case / "failure-message.txt").read_text(),
                )
            if invalid_quotes or invalid_executable:
                self.assertFalse((case / "observed-directory.txt").exists())
        else:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(Path((case / "installed-directory.txt").read_text()), new)
            if not missing:
                self.assertEqual(Path((case / "observed-directory.txt").read_text()), old)
                self.assertFalse((old / "sentinel.txt").exists())
            else:
                self.assertTrue((old / "sentinel.txt").exists())
            if not same and new_exists:
                self.assertEqual((new / "sentinel.txt").read_text(), "keep new destination")
        self.assertEqual(data.read_text(), "keep settings")

    def test_changed_destination_quoted(self) -> None:
        self._case("quoted")

    def test_changed_destination_unquoted(self) -> None:
        self._case("unquoted", quoted=False)

    def test_same_destination(self) -> None:
        self._case("same", same=True)

    def test_old_uninstaller_failure_aborts(self) -> None:
        self._case("failed", fail=True)

    def test_missing_old_uninstaller(self) -> None:
        self._case("missing", missing=True)

    def test_nested_destination_aborts_before_old_uninstaller(self) -> None:
        self._case("nested", nested=True)

    def test_nested_unquoted_destination_aborts(self) -> None:
        self._case("nested-unquoted", quoted=False, nested=True)

    def test_nonexistent_nested_destination_aborts(self) -> None:
        self._case("nested-not-created", nested=True, new_exists=False)

    def test_normalized_nested_destination_aborts(self) -> None:
        self._case("nested-normalized", nested=True, normalized_nested=True)

    def test_prefix_sibling_destination_is_allowed(self) -> None:
        self._case("prefix-sibling", prefix_sibling=True)

    def test_same_destination_trailing_separator(self) -> None:
        self._case("same-trailing-separator", same=True, trailing_separator=True)

    def test_nonexistent_different_destination_is_allowed(self) -> None:
        self._case("new-not-created", new_exists=False)

    def test_invalid_quoted_registration_aborts(self) -> None:
        self._case("invalid-quotes", invalid_quotes=True)

    def test_failure_to_launch_old_uninstaller_aborts(self) -> None:
        self._case("invalid-executable", invalid_executable=True)


if __name__ == "__main__":
    unittest.main()
