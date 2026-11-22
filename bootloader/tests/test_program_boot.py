import os
from pathlib import Path
import subprocess
import tempfile
import unittest


PROJECT = Path(__file__).resolve().parents[1]
SIZE = 28672
MOCK_PROGRAMMER = """\
#!/usr/bin/env python3
import os
from pathlib import Path
import sys

# Curent data in BOOTLOADER will be rewritten.
state = Path(os.environ["MOCK_STATE"])
command = sys.argv[1]
if command == "-i":
    print("Detected " + os.environ.get("MOCK_CHIP", "CH32V303"), file=sys.stderr)
    print("Read protection: " + os.environ.get("MOCK_PROTECTION", "disabled"), file=sys.stderr)
elif command == "-r":
    assert sys.argv[3:] == ["0x1fff8000", "28672"]
    data = state.read_bytes()
    counter = state.with_suffix(".reads")
    count = int(counter.read_text()) + 1 if counter.exists() else 1
    counter.write_text(str(count))
    if os.environ.get("MOCK_BAD_READ") == str(count):
        data = bytes([data[0] ^ 1]) + data[1:]
    Path(sys.argv[2]).write_bytes(data)
elif command == "-w":
    assert sys.argv[3] == "0x1fff8000"
    state.write_bytes(Path(sys.argv[2]).read_bytes())
    if os.environ.get("MOCK_BAD_WRITE") == "1":
        state.write_bytes(b"bad" + state.read_bytes()[3:])
else:
    sys.exit(1)
"""


class ProgrammingWorkflowTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.programmer = root / "minichlink"
        self.programmer.write_text(MOCK_PROGRAMMER)
        self.programmer.chmod(0o755)
        self.state = root / "state.bin"
        self.original = bytes(range(256)) * (SIZE // 256)
        self.state.write_bytes(self.original)
        self.backups = root / "backups"
        self.image = root / "hello.bin"
        self.image.write_bytes(b"hello" + b"\xff" * (SIZE - 5))
        self.env = os.environ | {
            "MINICHLINK": str(self.programmer),
            "MOCK_STATE": str(self.state),
            "BACKUP_DIR": str(self.backups),
        }

    def run_script(self, *args, answer="", **env):
        return self.run_script_from(PROJECT / "program-boot.sh", *args, answer=answer, **env)

    def run_script_from(self, script, *args, answer="", **env):
        return subprocess.run(
            ["bash", str(script), *map(str, args)],
            input=answer,
            text=True,
            capture_output=True,
            cwd=str(script.parent),
            env=self.env | env,
        )

    def test_backup_flash_and_restore(self):
        result = self.run_script("backup")
        self.assertEqual(result.returncode, 0, result.stderr)
        backup = next(self.backups.glob("*/bootloader.bin"))
        self.assertEqual(backup.read_bytes(), self.original)
        result = self.run_script("flash", self.image, answer="REPLACE BOOTLOADER\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.state.read_bytes(), self.image.read_bytes())
        self.assertEqual(len(list(self.backups.glob("*/bootloader.bin"))), 2)
        result = self.run_script("restore", backup, answer="REPLACE BOOTLOADER\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.state.read_bytes(), self.original)

    def test_rejects_bad_identification_and_read_protection(self):
        for overrides in ({"MOCK_CHIP": "CH32V307"}, {"MOCK_PROTECTION": "enabled"}):
            with self.subTest(overrides=overrides):
                result = self.run_script("backup", **overrides)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.backups.exists())

    def test_uniform_backup_and_cancel_do_not_write(self):
        self.state.write_bytes(b"\xff" * SIZE)
        result = self.run_script("flash", self.image, answer="REPLACE BOOTLOADER\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.state.read_bytes(), b"\xff" * SIZE)
        self.state.write_bytes(self.original)
        result = self.run_script("flash", self.image, answer="no\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.state.read_bytes(), self.original)

    def test_inconsistent_backup_does_not_write(self):
        result = self.run_script(
            "flash", self.image, answer="REPLACE BOOTLOADER\n", MOCK_BAD_READ="2"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("reads differ", result.stderr)
        self.assertEqual(self.state.read_bytes(), self.original)

    def test_bad_readback_and_modified_backup_fail(self):
        result = self.run_script("backup")
        self.assertEqual(result.returncode, 0, result.stderr)
        backup = next(self.backups.glob("*/bootloader.bin"))
        backup.write_bytes(b"bad" + self.original[3:])
        result = self.run_script("restore", backup, answer="REPLACE BOOTLOADER\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.state.read_bytes(), self.original)
        result = self.run_script(
            "flash", self.image, answer="REPLACE BOOTLOADER\n", MOCK_BAD_WRITE="1"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Read-back does not match", result.stderr)

    def test_incompatible_programmer_is_rejected(self):
        """A programmer without the boot-region support must not be used."""
        # A mock without the CH32V30x boot-region diagnostic. The script
        # falls back to ./tools/minichlink when one is present, so the test
        # runs a copy of the script in a directory without that fallback.
        self.programmer.write_text("#!/bin/sh\nexit 1\n")
        self.programmer.chmod(0o755)
        script = Path(self.temp.name) / "program-boot.sh"
        script.write_text((PROJECT / "program-boot.sh").read_text())
        result = self.run_script_from(script, "backup")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("boot-region support", result.stderr)
        self.assertFalse(self.backups.exists())

    def test_local_programmer_is_used_as_a_fallback(self):
        """An old programmer falls back to the locally built one."""
        old = Path(self.temp.name) / "old-minichlink"
        old.write_text("#!/bin/sh\nexit 1\n")
        old.chmod(0o755)

        # The script is run from a directory that holds a "local" programmer,
        # so the fallback is exercised without touching the real one.
        sandbox = Path(self.temp.name) / "sandbox"
        (sandbox / "tools").mkdir(parents=True)
        (sandbox / "program-boot.sh").write_text((PROJECT / "program-boot.sh").read_text())
        local_programmer = sandbox / "tools" / "minichlink"
        local_programmer.write_text(MOCK_PROGRAMMER)
        local_programmer.chmod(0o755)

        state = sandbox / "state.bin"
        state.write_bytes(self.original)
        env = {
            "MINICHLINK": str(old),
            "MOCK_STATE": str(state),
            "BACKUP_DIR": str(sandbox / "backups"),
        }
        result = self.run_script_from(sandbox / "program-boot.sh", "backup", **env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("locally built programmer", result.stdout)
        backup = next((sandbox / "backups").glob("*/bootloader.bin"))
        self.assertEqual(backup.read_bytes(), self.original)


if __name__ == "__main__":
    unittest.main()
