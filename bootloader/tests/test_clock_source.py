"""
Checks that the bootloader uses the same clock source as the cartridge firmware.

The RISKYMSX board has no external crystal, so the HSE clock settings never
start. The WCH clock setup then leaves the core on the 8 MHz internal
oscillator while SystemCoreClock still reports the HSE frequency, which makes
the USART baud rate and the delay timers wrong by the same factor. The
bootloader would then produce no visible output at all.
"""

import pathlib
import re
import unittest

PROJECT = pathlib.Path(__file__).resolve().parents[1]
REPO = PROJECT.parent
BOOT_CLOCK = PROJECT / "User" / "system_ch32v30x.c"
FIRMWARE_CLOCK = REPO / "firmware" / "User" / "system_ch32v30x.c"


def active_clock_defines(text):
    """Returns the clock defines that are not commented out."""
    defines = []
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("//"):
            continue
        match = re.match(r"#define\s+(SYSCLK_FREQ_\w+)", stripped)
        if match:
            defines.append(match.group(1))
    return defines


class ClockSourceTest(unittest.TestCase):
    def test_bootloader_uses_the_internal_oscillator(self):
        defines = active_clock_defines(BOOT_CLOCK.read_text())
        self.assertEqual(
            len(defines),
            1,
            "expected exactly one active clock setting, got %s" % defines,
        )
        self.assertIn(
            "HSI",
            defines[0],
            "the bootloader must run from the internal oscillator: the board "
            "has no crystal, so an HSE setting would leave the core on 8 MHz "
            "while the code believes it is running faster",
        )

    def test_bootloader_matches_the_cartridge_firmware(self):
        firmware = active_clock_defines(FIRMWARE_CLOCK.read_text())
        bootloader = active_clock_defines(BOOT_CLOCK.read_text())
        self.assertEqual(
            bootloader,
            firmware,
            "the bootloader and the cartridge firmware must use the same "
            "clock source",
        )


if __name__ == "__main__":
    unittest.main()
