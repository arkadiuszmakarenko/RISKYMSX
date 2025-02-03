#ifndef __MSXTerminal_H
#define __MSXTerminal_H

typedef enum page {
    MAIN,
    MAPPER

} MenuType;

void Init_MSXTerminal (void);

void PrintMainMenu (int page);
void PrintMapperMenu();
void ProcessMSXTerminal (void);
void NewLine(void);
void MoveCursor(int x, int y);
void ClearScreen();
void CursorUp();
void CursorDown();

#endif /* __MSXTerminal_H */
