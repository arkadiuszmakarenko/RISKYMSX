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
#define MAX_DIR_DEPTH 60

typedef struct TerminalPageState {
    MenuType pageName;

    uint32_t FileIndex;
    uint32_t FileIndexSize;
    uint32_t FileIndexPage;
    uint16_t FileOffsetStack[MAX_DIR_DEPTH]; // stores file index selected when entering a directory (indexed by directory depth)
    uint8_t  DirDepth;                       // current directory depth
    uint32_t CartTypeIndex;
    uint8_t folder[256];
    FileEntry FileArray[FILE_ARRAY_SIZE];
    uint8_t Filename[256];
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