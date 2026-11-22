#include "termui.h"
#include "msxbus.h"
#include "boot_debug.h"

#if BOOTLOADER_DEBUG
extern int _write(int fd, char *buf, int size);
#endif

/*
 * Terminal rendering for the MSX side program.
 *
 * The MSX side treats the output byte stream as a mix of printable text and
 * ANSI escape sequences, plus a private 3 byte command that moves the arrow
 * cursor. The escape sequences are the same ones the cartridge firmware uses.
 */

static void QueueByte(uint8_t value)
{
    MsxBus_QueueByte(value);
}

static void WriteTextByte(uint8_t value)
{
#if BOOTLOADER_DEBUG
    char byte = (char)value;
    _write(1, &byte, 1);
#endif
    QueueByte(value);
}

static void QueueString(const char *text)
{
    while (*text) {
        WriteTextByte((uint8_t)*text++);
    }
}

void TermUi_Init(void)
{
    TermUi_Clear();
}

void TermUi_Clear(void)
{
    QueueByte(0x1B);
    QueueByte(0x45);
}

void TermUi_MoveCursor(int x, int y)
{
    QueueByte(0x1B);
    QueueByte(0x59);
    QueueByte((uint8_t)(0x20 + y));
    QueueByte((uint8_t)(0x20 + x));
}

void TermUi_MovePointer(int x, int y)
{
    QueueByte(0x04);
    QueueByte((uint8_t)(x * 8));
    QueueByte((uint8_t)(y * 8));
}

void TermUi_NewLine(void)
{
    WriteTextByte(0x0D);
    WriteTextByte(0x0A);
}

void TermUi_Write(const char *text)
{
    QueueString(text);
}

void TermUi_WriteNumber(uint32_t value)
{
    char digits[11];
    int length = 0;

    do {
        digits[length++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0);

    while (length > 0) {
        WriteTextByte((uint8_t)digits[--length]);
    }
}

void TermUi_WriteSize(uint32_t bytes)
{
    uint32_t kilobytes = bytes / 1024;

    if (kilobytes == 0) {
        TermUi_WriteNumber(bytes);
        TermUi_Write(" B");
        return;
    }

    TermUi_WriteNumber(kilobytes);
    TermUi_Write(" KB");
}

void TermUi_WriteAt(int x, int y, const char *text)
{
    TermUi_MoveCursor(x, y);
    QueueString(text);
}

void TermUi_Flush(void)
{
    MsxBus_Flush();
}

/* Leave the last column untouched to avoid automatic wrapping. */
static void StatusLine(const char *text)
{
    char line[TERM_COLUMNS];
    int i = 0;

    while (i < TERM_COLUMNS - 1 && text[i]) {
        line[i] = text[i];
        i++;
    }
    while (i < TERM_COLUMNS - 1) {
        line[i] = ' ';
        i++;
    }
    line[i] = '\0';

    TermUi_WriteAt(0, TERM_ROWS - 1, line);
}

void TermUi_Status(const char *text)
{
    StatusLine(text);
}

void TermUi_Info(const char *text)
{
    TermUi_Write(text);
    TermUi_NewLine();
}

void TermUi_Success(const char *text)
{
    TermUi_Write(text);
    TermUi_NewLine();
}

void TermUi_Error(const char *text)
{
    TermUi_Write("ERROR: ");
    TermUi_Write(text);
    TermUi_NewLine();
}

/* Redraws the progress line in the middle of the screen. */
void TermUi_Progress(uint8_t percent)
{
    char line[20];
    int i = 0;
    const char *label = "Programming: ";
    const char *digits = "0123456789";

    while (label[i]) {
        line[i] = label[i];
        i++;
    }
    line[i++] = (char)digits[(percent / 100) % 10];
    line[i++] = (char)digits[(percent / 10) % 10];
    line[i++] = (char)digits[percent % 10];
    line[i++] = '%';
    line[i] = '\0';
    TermUi_WriteAt(6, 11, line);
#if BOOTLOADER_DEBUG
    _write(1, "\r\n", 2);
#endif
}