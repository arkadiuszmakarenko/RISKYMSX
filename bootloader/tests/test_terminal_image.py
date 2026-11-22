"""
Checks that the MSX side program embedded in the bootloader matches the
assembled binary, and that the bootloader's variant differs from the
cartridge firmware's variant only in the GRAPH key check.
"""

import pathlib
import re
import unittest

PROJECT = pathlib.Path(__file__).resolve().parents[1]
REPO = PROJECT.parent
MSX_DIR = REPO / "MSXTerminal"

BOOT_BINARY = MSX_DIR / "msxterm_boot.bin"
FIRMWARE_BINARY = MSX_DIR / "msxterm.bin"
BOOT_SOURCE = MSX_DIR / "msxterminal_boot.asm"
FIRMWARE_SOURCE = MSX_DIR / "msxterminal.asm"
MSXBUS = PROJECT / "User" / "msxbus.c"


def parse_c_array(text, name):
    """Reads the bytes of a C array initialiser."""
    match = re.search(
        r"static const uint8_t %s\[(\d+)\] = \{(.*?)\n\};" % name, text, re.S
    )
    assert match, "array %s not found" % name
    declared = int(match.group(1))
    body = match.group(2)

    values = []
    for token in body.split(","):
        token = token.strip()
        if not token:
            continue
        if token.startswith("'"):
            values.append(ord(token[1]))
        elif token.startswith("0x"):
            values.append(int(token, 16))
        elif token.startswith("\\"):
            values.append(int(token[1:], 8))
        else:
            values.append(int(token))
    return declared, bytes(values)


class TerminalImageTest(unittest.TestCase):
    def test_embedded_image_matches_the_assembled_binary(self):
        declared, embedded = parse_c_array(MSXBUS.read_text(), "msxterm")
        binary = BOOT_BINARY.read_bytes()
        self.assertEqual(
            len(binary),
            declared,
            "the array size in msxbus.c does not match the binary",
        )
        self.assertEqual(
            embedded,
            binary,
            "the embedded terminal image differs from msxterm_boot.bin",
        )

    def test_bootloader_variant_differs_from_the_firmware_variant(self):
        """The bootloader must not jump straight to the ROM on GRAPH."""
        boot = BOOT_BINARY.read_bytes()
        firmware = FIRMWARE_BINARY.read_bytes()
        self.assertNotEqual(
            boot,
            firmware,
            "the bootloader terminal is identical to the firmware terminal",
        )
        self.assertLess(len(boot), len(firmware))

    def test_bootloader_source_has_no_graph_check(self):
        source = BOOT_SOURCE.read_text()
        firmware_source = FIRMWARE_SOURCE.read_text()

        self.assertIn(
            "Check if GRPH-key pressed", firmware_source,
            "the firmware source no longer checks the GRAPH key",
        )
        self.assertNotIn(
            "Check if GRPH-key pressed", source,
            "the bootloader source must not check the GRAPH key",
        )
        self.assertIn(
            "LD A,6", firmware_source,
            "the firmware source no longer polls the keyboard for GRAPH",
        )
        self.assertNotIn(
            "LD A,6", source,
            "the bootloader source must not poll the keyboard for GRAPH",
        )

    def test_firmware_binary_is_unchanged(self):
        """The cartridge firmware keeps the original terminal program."""
        firmware = FIRMWARE_BINARY.read_bytes()
        self.assertEqual(len(firmware), 213)
        self.assertTrue(firmware.startswith(b"AB&@"))
        self.assertIn(b"RISKY_BOOT", firmware)


if __name__ == "__main__":
    unittest.main()
