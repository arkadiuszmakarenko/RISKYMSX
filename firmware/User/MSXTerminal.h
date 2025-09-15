#ifndef __MSXTerminal_H
#define __MSXTerminal_H

#include "cart.h"

typedef enum page {
    MAIN,
    MAPPER,
    FOLDER,
    CHANGEMAPPER

} MenuType;

#define FILE_ARRAY_SIZE 20

typedef struct TerminalPageState {
    MenuType pageName;

    uint32_t FileIndex;
    uint32_t FileIndexSize;
    uint32_t FileIndexPage;
    uint32_t CartTypeIndex;
    uint8_t *folder;
    FileEntry *FileArray[FILE_ARRAY_SIZE];
    uint8_t *Filename;
} MenuState;

void Init_MSXTerminal (void);

void PrintMainMenu (int page);
void PrintMapperMenu();
void ChangeMapperMenu();
void ProcessMSXTerminal (void);
void NewLine (void);
void MoveCursor (int x, int y);
void MovePointer (int x, int y);
void ClearScreen();
void CursorUp();
void CursorDown();
void Reset();
void PrintMapperType (CartType type);
void PrintMapperList();
void ResetPointer();

#endif /* __MSXTerminal_H */