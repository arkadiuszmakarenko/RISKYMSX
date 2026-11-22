#ifndef TERM_UI_H
#define TERM_UI_H

#include <stdint.h>

/* Screen geometry of the MSX text mode used by the terminal program. */
#define TERM_COLUMNS 32
#define TERM_ROWS 24

void TermUi_Init(void);
void TermUi_Clear(void);
void TermUi_MoveCursor(int x, int y);
void TermUi_MovePointer(int x, int y);
void TermUi_NewLine(void);
void TermUi_Write(const char *text);
void TermUi_WriteAt(int x, int y, const char *text);
void TermUi_Flush(void);
void TermUi_Status(const char *text);
void TermUi_Progress(uint8_t percent);
void TermUi_Error(const char *text);
void TermUi_Info(const char *text);
void TermUi_Success(const char *text);

/* Writes a decimal number at the cursor position. */
void TermUi_WriteNumber(uint32_t value);
/* Writes "<number> <unit>", e.g. "2048 KB". */
void TermUi_WriteSize(uint32_t bytes);

#endif /* TERM_UI_H */
