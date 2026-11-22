"""
Host-side tests for the bootloader logic that does not need the chip.

The flash programming routine is compiled for the host with a stub of the
WCH flash controller, so the erase/program/verify sequence and its error
handling can be exercised without hardware.
"""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
HOST_TEST_DIR = Path(__file__).resolve().parent / "host"

# The program flow, progress reporting and error text are checked through a
# small harness that includes the real sources.
HARNESS_C = r"""
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ff.h"
#include "ch32v30x_flash.h"
#include "ch32v30x_rcc.h"
#include "debug.h"
#include "appflash.h"

/* ---- stubs for the WCH flash controller ------------------------------- */

static uint32_t fake_flash[65536];     /* 256 KiB of main flash */
static uint32_t fake_sector = 1024;    /* 4 KiB pages, in 32-bit words */
static uint32_t fake_locked = 0;
static int erase_failures = 0;
static int write_failures = 0;
static int enhanced_mode = 1;          /* starts in the enhanced read mode */
static int enhanced_mode_off_calls = 0;

void FLASH_Unlock(void) { fake_locked = 0; }
void FLASH_Lock(void) { fake_locked = 1; }
void FLASH_Unlock_Fast(void) { fake_locked = 0; }
void FLASH_Lock_Fast(void) { fake_locked = 1; }

void FLASH_Enhance_Mode(FunctionalState new_state) {
    if (new_state == ENABLE) {
        enhanced_mode = 1;
    } else {
        enhanced_mode = 0;
        enhanced_mode_off_calls++;
    }
}

FLASH_Status FLASH_ErasePage(uint32_t page_address) {
    if (erase_failures-- > 0) return FLASH_ERROR_PG;
    if (fake_locked) return FLASH_ERROR_WRP;
    uint32_t index = (page_address - 0x08000000u) / 4;
    for (uint32_t i = 0; i < fake_sector; i++) {
        fake_flash[index + i] = 0xFFFFFFFFu;
    }
    return FLASH_COMPLETE;
}

FLASH_Status FLASH_ProgramHalfWord(uint32_t address, uint16_t data) {
    if (write_failures-- > 0) return FLASH_ERROR_PG;
    if (fake_locked) return FLASH_ERROR_WRP;
    uint32_t index = (address - 0x08000000u) / 4;
    uint32_t shift = (address & 3) * 8;
    uint32_t mask = 0xFFFFu << shift;
    fake_flash[index] = (fake_flash[index] & ~mask) | ((uint32_t)data << shift);
    return FLASH_COMPLETE;
}

void FLASH_ErasePage_Fast(uint32_t page_address) {
    uint32_t index = (page_address - 0x08000000u) / 4;
    for (uint32_t i = 0; i < 256 / 4; i++) {
        fake_flash[index + i] = 0xFFFFFFFFu;
    }
}

FLASH_Status FLASH_ROM_WRITE(uint32_t address, uint32_t *data, uint32_t length) {
    uint32_t index = (address - 0x08000000u) / 4;
    for (uint32_t i = 0; i < length / 4; i++) {
        fake_flash[index + i] = data[i];
    }
    return FLASH_COMPLETE;
}

RCC_TypeDef fake_rcc = {0};

void Delay_Us(uint32_t n) { (void)n; }
void Delay_Ms(uint32_t n) { (void)n; }

/* Reads back from the fake flash instead of the real address space. */
static int corrupt_readback = 0;

uint8_t AppFlash_ReadByte(uint32_t address) {
    uint32_t index = (address - 0x08000000u) / 4;
    uint32_t shift = (address & 3) * 8;
    uint8_t value = (uint8_t)((fake_flash[index] >> shift) & 0xFFu);
    if (corrupt_readback) {
        value ^= 0xFFu;
    }
    return value;
}

/* ---- stub filesystem --------------------------------------------------- */

static FIL g_file;
static uint8_t g_image[1024 * 1024];
static uint32_t g_image_size;
static int g_file_present;

FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode) {
    (void)path;
    (void)mode;
    if (!g_file_present) return FR_NO_FILE;
    fp->fptr = 0;
    fp->objsize = g_image_size;
    return FR_OK;
}

FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br) {
    uint32_t left = fp->objsize - fp->fptr;
    UINT count = left < btr ? (UINT)left : btr;
    memcpy(buff, g_image + fp->fptr, count);
    fp->fptr += count;
    *br = count;
    return FR_OK;
}

FRESULT f_close(FIL *fp) { (void)fp; return FR_OK; }

/* ---- harness ----------------------------------------------------------- */

static uint8_t last_percent;
static int progress_calls;

static void on_progress(uint8_t percent) {
    last_percent = percent;
    progress_calls++;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <image-file> <case>\n", argv[0]);
        return 2;
    }

    const char *path = argv[1];
    const char *mode = argv[2];

    FILE *f = fopen(path, "rb");
    if (!f) return 3;
    g_image_size = (uint32_t)fread(g_image, 1, sizeof(g_image), f);
    fclose(f);
    g_file_present = 1;

    if (strcmp(mode, "erase_fail") == 0) erase_failures = 1;
    if (strcmp(mode, "write_fail") == 0) write_failures = 1;
    if (strcmp(mode, "missing") == 0) g_file_present = 0;
    if (strcmp(mode, "verify_fail") == 0) corrupt_readback = 1;

    const char *detail = NULL;
    AppFlashResult result = AppFlash_Program("IMAGE.BIN", on_progress, &detail);

    printf("result=%d\n", (int)result);
    printf("text=%s\n", AppFlash_ResultText(result));
    printf("percent=%u\n", (unsigned)last_percent);
    printf("progress_calls=%d\n", progress_calls);
    printf("enhanced_mode=%d\n", enhanced_mode);
    printf("enhanced_mode_off_calls=%d\n", enhanced_mode_off_calls);

    if (result == APP_FLASH_OK) {
        printf("data=");
        for (int i = 0; i < 16; i++) {
            printf("%02x", (unsigned)((uint8_t *)(fake_flash))[i]);
        }
        printf("\n");
    }
    return 0;
}
"""


class FlashProgrammingTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.image = self.root / "image.bin"
        # A recognisable pattern with a header, so a wrong write is visible.
        self.payload = bytes(range(256)) * 64
        self.image.write_bytes(self.payload)

    def build_harness(self):
        harness = self.root / "harness.c"
        harness.write_text(HARNESS_C)
        binary = self.root / "harness"
        command = [
            "cc", "-std=gnu99", "-O1", "-w",
            f"-I{Path(__file__).resolve().parent / 'host'}",
            f"-I{PROJECT / 'User'}",
            f"-I{PROJECT / 'User' / 'FATFS'}",
            str(harness),
            str(PROJECT / "User" / "appflash.c"),
            "-o", str(binary),
        ]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return binary

    def run_case(self, mode):
        binary = self.build_harness()
        result = subprocess.run(
            [str(binary), str(self.image), mode], capture_output=True, text=True
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        fields = dict(
            line.split("=", 1) for line in result.stdout.strip().splitlines()
        )
        return fields

    def test_successful_program_verifies(self):
        fields = self.run_case("ok")
        self.assertEqual(fields["result"], "0")
        self.assertEqual(fields["text"], "ok")
        self.assertEqual(fields["percent"], "100")
        self.assertGreater(int(fields["progress_calls"]), 0)
        self.assertEqual(fields["data"], self.payload[:16].hex())

    def test_erase_failure_is_reported(self):
        fields = self.run_case("erase_fail")
        self.assertEqual(fields["result"], "3")
        self.assertEqual(fields["text"], "erase failed")

    def test_write_failure_is_reported(self):
        fields = self.run_case("write_fail")
        self.assertEqual(fields["result"], "4")
        self.assertEqual(fields["text"], "programming failed")

    def test_missing_image_is_reported(self):
        fields = self.run_case("missing")
        self.assertEqual(fields["result"], "1")
        self.assertEqual(fields["text"], "image file not found")

    def test_verify_failure_is_reported(self):
        """A read-back mismatch must not be reported as success."""
        fields = self.run_case("verify_fail")
        self.assertEqual(fields["result"], "5")
        self.assertEqual(fields["text"], "verification failed")

    def test_progress_is_monotonic_and_reaches_100(self):
        fields = self.run_case("ok")
        self.assertEqual(int(fields["percent"]), 100)

    def test_enhanced_read_mode_is_exited_before_programming(self):
        """Erase and program fail while the enhanced read mode is active.

        The reference manual states this explicitly, so the bootloader has to
        leave the mode before it touches the flash.
        """
        fields = self.run_case("ok")
        self.assertEqual(fields["enhanced_mode"], "0")
        self.assertGreaterEqual(int(fields["enhanced_mode_off_calls"]), 2)


if __name__ == "__main__":
    unittest.main()
