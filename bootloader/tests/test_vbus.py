"""
Checks that the bootloader powers the USB host port the way the board needs.

The RISKYMSX board feeds the USB host port through a CH217K current limited
switch. Its EN# input is active low and is wired to PB12, so the port stays
unpowered until the firmware drives PB12 low. FLAG# is an active low open
drain fault output on PB13.

Without driving PB12 low the switch stays off and no USB device can be seen
on the bus, which is exactly the "Cannot continue without a USB drive" case
the bootloader reported.
"""

import pathlib
import re
import unittest

PROJECT = pathlib.Path(__file__).resolve().parents[1]
REPO = PROJECT.parent
BOOT_BUS = PROJECT / "User" / "msxbus.c"
BOOT_MAIN = PROJECT / "User" / "main.c"
FIRMWARE_GPIO = REPO / "firmware" / "User" / "gpio.c"


def function_body(text, name):
    """Returns the body of a C function, from its opening brace to the
    matching closing brace."""
    match = re.search(
        r"^[A-Za-z_][\w \*]*\b%s\s*\([^)]*\)\s*\{" % re.escape(name),
        text,
        re.MULTILINE,
    )
    if not match:
        return None
    start = match.end() - 1
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    return None


class VbusTest(unittest.TestCase):
    def setUp(self):
        self.bus = BOOT_BUS.read_text()
        self.main = BOOT_MAIN.read_text()
        self.firmware = FIRMWARE_GPIO.read_text()

    def test_enable_drives_pb12_low(self):
        """EN# is active low, so enabling the port means clearing PB12."""
        body = function_body(self.bus, "Vbus_Enable")
        self.assertIsNotNone(body, "Vbus_Enable is missing")
        self.assertIn(
            "GPIO_ResetBits(GPIOB, GPIO_Pin_12)",
            body,
            "Vbus_Enable must clear PB12: the CH217K EN# input is active low",
        )

    def test_disable_drives_pb12_high(self):
        body = function_body(self.bus, "Vbus_Disable")
        self.assertIsNotNone(body, "Vbus_Disable is missing")
        self.assertIn(
            "GPIO_SetBits(GPIOB, GPIO_Pin_12)",
            body,
            "Vbus_Disable must set PB12: the CH217K EN# input is active low",
        )

    def test_fault_flag_reads_pb13(self):
        """FLAG# is active low, so a fault is a zero on PB13."""
        body = function_body(self.bus, "Vbus_Faulted")
        self.assertIsNotNone(body, "Vbus_Faulted is missing")
        self.assertIn("GPIO_Pin_13", body)
        self.assertIn("Bit_RESET", body)

    def test_pin_assignment_matches_the_cartridge_firmware(self):
        """The bootloader must use the same pins as the working firmware."""
        firmware_enable = function_body(self.firmware, "VBUS_Enable")
        firmware_fault = function_body(self.firmware, "VBUS_Fault")
        self.assertIsNotNone(firmware_enable)
        self.assertIsNotNone(firmware_fault)
        self.assertIn("GPIO_Pin_12", firmware_enable)
        self.assertIn("GPIO_Pin_13", firmware_fault)

    def test_bootloader_enables_vbus_before_usb_initialisation(self):
        """The switch has to be on before the host controller looks for a
        device, otherwise the bus reads as empty."""
        enable = self.main.find("Vbus_Enable()")
        usb = self.main.find("USB_Initialization()")
        self.assertNotEqual(enable, -1, "the bootloader never enables VBUS")
        self.assertNotEqual(usb, -1)
        self.assertLess(
            enable,
            usb,
            "VBUS must be enabled before the USB host is initialised",
        )

    def test_vbus_is_configured_as_an_output(self):
        body = function_body(self.bus, "Vbus_Init")
        self.assertIsNotNone(body, "Vbus_Init is missing")
        self.assertIn("GPIO_Pin_12", body)
        self.assertIn("GPIO_Mode_Out_PP", body)


if __name__ == "__main__":
    unittest.main()
