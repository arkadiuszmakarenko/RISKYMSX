#ifndef __MSXTerminal_H
#define __MSXTerminal_H

#include "cart.h"

typedef enum page {
    MAIN,
    MAPPER,
    FOLDER,
    CHANGEMAPPER

} MenuType;

void Init_MSXTerminal (void);

void PrintMainMenu (int page);
void PrintMapperMenu();
void ChangeMapperMenu();
void ProcessMSXTerminal (void);
void NewLine (void);
void MoveCursor (int x, int y);
void ClearScreen();
void CursorUp();
void CursorDown();
void Reset();
void PrintMapperType (CartType type);
void PrintMapperList();

#endif /* __MSXTerminal_H */
