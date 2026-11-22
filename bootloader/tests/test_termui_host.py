import pathlib
import re
import subprocess
import tempfile
import unittest

PROJECT = pathlib.Path(__file__).resolve().parents[1]

HARNESS = r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "termui.h"
#include "boot_debug.h"

static char screen[4096], serial[4096];
static unsigned screen_len, serial_len;

void MsxBus_QueueByte(uint8_t byte) {
    assert(screen_len < sizeof(screen));
    screen[screen_len++] = (char)byte;
}

void MsxBus_Flush(void) {}

int _write(int fd, char *buf, int size) {
    assert(fd == 1 && size >= 0);
    assert(serial_len + (unsigned)size <= sizeof(serial));
    memcpy(serial + serial_len, buf, size);
    serial_len += size;
    return size;
}

int main(void) {
    TermUi_Clear();
    TermUi_MoveCursor(0, 0);
    TermUi_Write("USB drive: found");
    TermUi_NewLine();
    TermUi_Write("  Address:");
    TermUi_WriteNumber(42);
    TermUi_NewLine();
    TermUi_Error("No image");

    const char expected[] = "USB drive: found\r\n  Address:42\r\nERROR: No image\r\n";
#if BOOTLOADER_DEBUG
    assert(serial_len == sizeof(expected) - 1);
    assert(memcmp(serial, expected, serial_len) == 0);
#else
    assert(serial_len == 0);
#endif
    assert(screen_len == sizeof(expected) - 1 + 6);
    assert(memcmp(screen, "\x1b" "E\x1b" "Y  ", 6) == 0);
    assert(memcmp(screen + 6, expected, sizeof(expected) - 1) == 0);

    screen_len = serial_len = 0;
    TermUi_Progress(0);
    TermUi_Progress(42);
    TermUi_Progress(100);
    const char first[] = "\x1b" "Y" "\x2b\x26" "Programming: 000%";
    const char second[] = "\x1b" "Y" "\x2b\x26" "Programming: 042%";
    const char third[] = "\x1b" "Y" "\x2b\x26" "Programming: 100%";
    const unsigned length = sizeof(first) - 1;
    assert(screen_len == 3 * length);
    assert(memcmp(screen, first, length) == 0);
    assert(memcmp(screen + length, second, length) == 0);
    assert(memcmp(screen + 2 * length, third, length) == 0);
#if BOOTLOADER_DEBUG
    const char progress_serial[] =
        "Programming: 000%\r\nProgramming: 042%\r\nProgramming: 100%\r\n";
    assert(serial_len == sizeof(progress_serial) - 1);
    assert(memcmp(serial, progress_serial, serial_len) == 0);
#else
    assert(serial_len == 0);
#endif

    screen_len = serial_len = 0;
    for (int i = 0; i < 400; i++) {
        TermUi_Write("x");
    }
    TermUi_NewLine();
    assert(screen_len == 402);
#if BOOTLOADER_DEBUG
    assert(serial_len == 402);
    assert(memcmp(screen, serial, 402) == 0);
    assert(memcmp(serial + 400, "\r\n", 2) == 0);
#else
    assert(serial_len == 0);
    assert(memcmp(screen + 400, "\r\n", 2) == 0);
#endif
    return 0;
}
"""


class TermUiHostTest(unittest.TestCase):
    def test_confirmation_clears_and_homes_before_both_outcomes(self):
        main = (PROJECT / "User" / "main.c").read_text()
        self.assertRegex(
            main,
            r"int confirmed = wait_for_confirm\(30000\);\s*"
            r"TermUi_Clear\(\);\s*TermUi_MoveCursor\(0, 0\);\s*"
            r"if \(!confirmed\)",
        )

    def test_programming_updates_are_drained_before_flash_work(self):
        main = (PROJECT / "User" / "main.c").read_text()
        self.assertRegex(
            main, re.compile(
                r"TermUi_Progress\(0\);\s*TermUi_Flush\(\);\s*"
                r"Delay_Ms\(20\);.*?AppFlash_Program", re.S,
            ),
        )
        callback = re.search(
            r"static void progress_callback\(uint8_t percent\)\s*\{(.*?)\n\}",
            main, re.S,
        )
        self.assertIsNotNone(callback)
        self.assertRegex(callback.group(1), r"TermUi_Flush\(\);\s*Delay_Ms\(20\);")

    def test_text_newlines_and_progress_on_both_outputs(self):
        for debug in (0, 1):
            with self.subTest(debug=debug), tempfile.TemporaryDirectory() as directory:
                directory = pathlib.Path(directory)
                source = directory / "harness.c"
                executable = directory / "harness"
                source.write_text(HARNESS)
                result = subprocess.run(
                    ["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                     "-D__attribute__(x)=", f"-DBOOTLOADER_DEBUG={debug}",
                     "-I", str(PROJECT / "User"), str(source),
                     str(PROJECT / "User" / "termui.c"), "-o", str(executable)],
                    capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([str(executable)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_queue_backpressure_preserves_bytes(self):
        source_text = (PROJECT / "User" / "msxbus.c").read_text()
        match = re.search(
            r"void MsxBus_QueueByte\(uint8_t value\)\s*\{.*?\n\}",
            source_text, re.S,
        )
        self.assertIsNotNone(match)
        harness = r"""
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#define __attribute__(x)
#include "msxbus.h"
static MsxOutQueue *queue;
#define msx_out (*queue)
""" + match.group() + r"""
int main(void) {
    queue = mmap(0, sizeof(*queue), PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    assert(queue != MAP_FAILED);
    pid_t writer = fork();
    assert(writer >= 0);
    if (writer == 0) {
        for (int i = 0; i < 1024; i++) MsxBus_QueueByte((uint8_t)i);
        _exit(0);
    }
    for (int i = 0; i < 2000 && queue->head != MSX_OUT_SIZE - 1; i++)
        usleep(1000);
    assert(queue->head == MSX_OUT_SIZE - 1);
    for (int i = 0; i < 1024; i++) {
        while (queue->head == queue->tail) usleep(100);
        assert(queue->data[queue->tail] == (uint8_t)i);
        queue->tail = (uint8_t)((queue->tail + 1) & (MSX_OUT_SIZE - 1));
    }
    int status;
    assert(waitpid(writer, &status, 0) == writer);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            source = directory / "queue.c"
            executable = directory / "queue"
            source.write_text(harness)
            result = subprocess.run(
                ["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-I", str(PROJECT / "User"), str(source), "-o", str(executable)],
                capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run(
                [str(executable)], capture_output=True, text=True, timeout=10,
            )
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
