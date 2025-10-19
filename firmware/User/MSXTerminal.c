#include "MSXTerminal.h"
#include "utils.h"
#include "cart.h"
#include "ff.h"
#include "usb_disk.h"
#include "programflash.h"
#include "version.h"


extern CircularBuffer cb;
CircularBuffer scb;
CircularBuffer icb;
MenuState menu;
uint32_t volatile enableTerminal = 0;

int PointerX = 1;
int PointerY = 1;

// Shared mapper names for both the type display and the selection list
static const char *const kMapperNames[10] = {
    "Standard 16KB ROM",
    "Standard 32KB ROM",
    "Standard 48KB or 64KB ROM",
    "KONAMI without SCC",
    "KONAMI with SCC (EN)",
    "KONAMI with SCC (DIS)",
    "ASCII 8KB",
    "ASCII 16KB",
    "NEO 8KB",
    "NEO 16KB"};

FATFS fs;

void MountFilesystem (void) {
    FRESULT fres;

    // Mount the filesystem
    do {
        fres = f_mount (&fs, "", 1);
        if (fres != FR_OK) {
            Delay_Ms (500);
        }
    } while (fres != FR_OK);
}

void AutoProgramCart (char *Filename, CartType cartType) {
    FIL f;
    // Try opening the file for read to detect existence
    if (f_open (&f, Filename, FA_READ) == FR_OK) {
        f_close (&f);
        ProgramCart (cartType, Filename, "/");
    }
}

void InitFileOffsetStack (void) {
    menu.DirDepth = 0;
    memset (menu.FileOffsetStack, 0, sizeof (menu.FileOffsetStack));
}

void StoreFileOffset (uint16_t fileOffset) {
    menu.FileOffsetStack[menu.DirDepth] = fileOffset;
}

uint16_t LoadFileOffset (void) {
    return menu.FileOffsetStack[menu.DirDepth];
}

uint16_t CalcCurrFileOffset (void) {
    return ((menu.FileIndexPage - 1) * FILE_ARRAY_SIZE) + menu.FileIndex;
}

void StoreCurrFileOffset (void) {
    StoreFileOffset (CalcCurrFileOffset());
}

void Init_MSXTerminal (void) {
    initBuffer (&scb);
    initMiniBuffer (&icb);

    while (enableTerminal == 0) { };

    // Buffers are now static in MenuState; no heap allocations
    memset (menu.Filename, 0, sizeof (menu.Filename));
    memset (menu.folder, 0, sizeof (menu.folder));
    InitFileOffsetStack();
    ClearScreen();

    GPIO_WriteBit (GPIOA, GPIO_Pin_0, Bit_RESET);
    GPIO_WriteBit (GPIOA, GPIO_Pin_1, Bit_RESET);
    GPIO_WriteBit (GPIOA, GPIO_Pin_2, Bit_RESET);
    GPIO_WriteBit (GPIOA, GPIO_Pin_3, Bit_RESET);

    GPIO_WriteBit (GPIOA, GPIO_Pin_8, Bit_RESET);

    appendString (&scb, " ");
    appendString (&scb, FIRMWARE_VERSION_STRING);
    NewLine();

    appendString (&scb, "Insert USB.");
    NewLine();
    NewLine();

    MountFilesystem();

    // auto program ROMS
    AutoProgramCart ("CART.R16", ROM16k);
    AutoProgramCart ("CART.R32", ROM32k);
    AutoProgramCart ("CART.R48", ROM48k);
    AutoProgramCart ("CART.KO4", KonamiWithoutSCC);
    AutoProgramCart ("CART.KO5", KonamiWithSCC);
    AutoProgramCart ("CART.KD5", KonamiWithSCCNOSCC);
    AutoProgramCart ("CART.A8K", ASCII8k);
    AutoProgramCart ("CART.A16", ASCII16k);
    AutoProgramCart ("CART.N16", NEO16);
    AutoProgramCart ("CART.N8K", NEO8);

    menu.FileIndexPage = 1;
    strcpy ((char *)menu.folder, "");
    flushBuffer (&icb);
    PrintMainMenu (menu.FileIndexPage);
}

void SizeToHumanReadableSize (char fileSizeStr[6], uint32_t fileSize) {
    uint32_t sizeAdjusted;
    int len;

    if (fileSize >= 1024 * 1024 * 1024) {
        sizeAdjusted = fileSize / (1024 * 1024 * 1024);
        len = intToString (sizeAdjusted, fileSizeStr);
        fileSizeStr[len++] = 'G';
    } else if (fileSize >= 1024 * 1024) {
        sizeAdjusted = fileSize / (1024 * 1024);
        len = intToString (sizeAdjusted, fileSizeStr);
        fileSizeStr[len++] = 'M';
    } else if (fileSize >= 1024) {
        sizeAdjusted = fileSize / 1024;
        len = intToString (sizeAdjusted, fileSizeStr);
        fileSizeStr[len++] = 'K';
    } else {
        len = intToString (fileSize, fileSizeStr);
    }
    fileSizeStr[len] = 'B';
    fileSizeStr[len + 1] = 0;
}

void PrintMainMenu (int page) {
    uint8_t FileNameSize[6];

    ClearScreen();

    menu.pageName = MAIN;
    if (!page) {
        // a zero page means we need to calculate the file page and file index from
        // the offset stored at the top of the file offset stack
        uint16_t fileOffset = LoadFileOffset();
        menu.FileIndexPage = (fileOffset / FILE_ARRAY_SIZE) + 1;
        menu.FileIndex = fileOffset % FILE_ARRAY_SIZE;
        PointerY = 1 + menu.FileIndex;
    } else {
        // non-zero page means start at first file index of that page
        menu.FileIndexPage = page;
        menu.FileIndex = 0;
        ResetPointer();
        StoreCurrFileOffset();
    }
    menu.FileIndexSize = listFiles (menu.folder, menu.FileArray, FILE_ARRAY_SIZE, menu.FileIndexPage);
    if (menu.FileIndex >= menu.FileIndexSize) {
        // if for some reason our file index is out of bounds, go to first file of current page
        menu.FileIndex = 0;
        StoreCurrFileOffset();
    }

    appendString (&scb, " ");
    appendString (&scb, FIRMWARE_VERSION_STRING);
    appendString (&scb, "    RISKY MSX ");
    appendString (&scb, "Page:");
    char pageString[5];
    intToString (page, pageString);
    appendString (&scb, pageString);
    NewLine();

    for (int i = 0; i < menu.FileIndexSize; i++) {
        MoveCursor (i + 1, 2);
        // Print filename, pad with 0x20 up to 25 chars
        for (int x = 0; x < 25; x++) {
            char c = menu.FileArray[i].name[x];
            if (c == 0x00) {
                // Pad the rest with 0x20
                for (; x < 25; x++) {
                    append (&scb, 0x20);
                }
                break;
            } else {
                append (&scb, c);
            }
        }

        if (menu.FileArray[i].isDir) {
            appendString (&scb, "<DIR> ");
        } else {
            uint32_t FileSize = menu.FileArray[i].size_kb;
            SizeToHumanReadableSize (FileNameSize, FileSize);
            appendString (&scb, FileNameSize);
            append (&scb, 0x20);
        }
        NewLine();
    }

    MoveCursor (21, 0);
    char location[64];
    strncpy (location, (char *)menu.folder, sizeof (location) - 1);
    location[sizeof (location) - 1] = '\0';
    appendString (&scb, location);

    MoveCursor (23, 0);
    appendString (&scb, " ARROWS,RET,ESC,1-MAP,BKSP-..");

    MoveCursor (1, 1);
    MovePointer (PointerX, PointerY);
}

void PrintMapperMenu() {
    uint8_t FileNameSize[6];

    ResetPointer();
    menu.pageName = MAPPER;
    ClearScreen();
    appendString (&scb, "          RISKY MSX ");
    NewLine();
    appendString (&scb, "  File to flash:");
    NewLine();
    append (&scb, 0x20);
    append (&scb, 0x20);
    appendStringUpToLen (&scb, (char *)menu.Filename, 24);
    append (&scb, 0x20);
    uint32_t FileSize = menu.FileArray[menu.FileIndex].size_kb;
    SizeToHumanReadableSize (FileNameSize, FileSize);
    appendString (&scb, FileNameSize);

    NewLine();
    PrintMapperList();

    MoveCursor (21, 0);

    uint8_t *location = (uint8_t *)malloc (64 * sizeof (uint8_t));
    strncpy ((char *)location, (char *)menu.folder, 63);
    location[63] = '\0';
    appendString (&scb, (char *)location);
    free (location);


    MoveCursor (23, 0);
    appendString (&scb, " UP,DOWN,RETURN,ESC ");
    MoveCursor (4, 1);
    PointerX = 1;
    PointerY = 4;
    MovePointer (PointerX, PointerY);
}

void ChangeMapperMenu() {
    PointerX = 1;
    PointerY = 4;
    MovePointer (PointerX, PointerY);
    char filesize[10] = {0};
    char file[64] = {0};
    CART_CFG volatile *cfg;
    // Read config from config flash location.
    uint8_t *restrict cfgpnt = (uint8_t *)&__cfg_section_start;
    cfg = (CART_CFG volatile *)cfgpnt;
    CartType volatile type;
    type = cfg->CartType;

    ClearScreen();
    appendString (&scb, " Filename and mapper type:");
    MoveCursor (1, 0);
    append (&scb, 0x20);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overread"
    strncpy (file, (char *)(cfgpnt + 8), 64);
    file[63] = '\0';
#pragma GCC diagnostic pop

    appendString (&scb, file);

    intToString ((uint16_t)cfg->CartSize, filesize);
    append (&scb, 0x20);
    appendString (&scb, filesize);
    appendString (&scb, "KB");


    MoveCursor (2, 0);
    PrintMapperType (type);
    MoveCursor (4, 0);
    PrintMapperList();
    MoveCursor (23, 0);
    appendString (&scb, " UP,DOWN,RETURN,ESC ");
    MoveCursor (4, 1);
}

void ProcessMSXTerminal (void) {
    uint32_t key;

    if (popmini (&icb, &key) == 0) {
        flushBuffer (&icb);
        switch (menu.pageName) {
        case MAIN:
            // ESC
            if (key == 0x1B) {
                ClearScreen();
                appendString (&scb, "Insert USB.");
                InitFileOffsetStack();
                menu.FileIndexPage = 1;
                strcpy ((char *)menu.folder, "");
                MountFilesystem();
                flushBuffer (&icb);
                PrintMainMenu (menu.FileIndexPage);
            }
            // backspace
            if (key == 0x08) {
                // Remove last section from menu.folder (go up one directory)
                size_t len = strlen ((char *)menu.folder);
                if (len > 0) {
                    // Remove trailing slash if present
                    if (menu.folder[len - 1] == '/' && len > 1) {
                        menu.folder[len - 1] = '\0';
                        len--;
                    }
                    // Find last slash
                    char *lastSlash = strrchr ((char *)menu.folder, '/');
                    if (lastSlash != NULL) {
                        // If not root, cut after last slash
                        if (lastSlash == (char *)menu.folder) {
                            // Go to root "/"
                            menu.folder[1] = '\0';
                        } else {
                            *lastSlash = '\0';
                        }
                    } else {
                        // No slash, go to empty (root)
                        menu.folder[0] = '\0';
                    }
                }
                int page;
                if (menu.DirDepth > 0) {
                    menu.DirDepth--;
                    page = 0;  // use page and index derived from file offset stack
                } else {
                    page = 1;  // use first page for root dir
                }
                ClearScreen();
                MovePointer (0xFF, 0xFF);
                PrintMainMenu (page);
            }
            // cursor right
            if (key == 0x1C) {
                if (menu.FileIndexSize < FILE_ARRAY_SIZE)
                    return;
                menu.FileIndexPage++;
                MovePointer (0xFF, 0xFF);
                PrintMainMenu (menu.FileIndexPage);
            }
            // cursor left
            if (key == 0x1D) {
                if (menu.FileIndexPage == 1)
                    return;
                menu.FileIndexPage--;
                MovePointer (0xFF, 0xFF);
                PrintMainMenu (menu.FileIndexPage);
            }
            // cursor up
            if (key == 0x1E) {
                if (menu.FileIndex != 0) {
                    menu.FileIndex--;
                    CursorUp();
                    StoreCurrFileOffset();
                }
            }
            // cursor down
            if (key == 0x1F && menu.FileIndexSize > 0) {
                if (menu.FileIndex == (menu.FileIndexSize - 1))
                    return;
                menu.FileIndex++;
                CursorDown();
                StoreCurrFileOffset();
            }
            // enter
            if (key == 0x0D && menu.FileIndexSize > 0) {
                menu.CartTypeIndex = 0;
                strcpy ((char *)menu.Filename, (char *)menu.FileArray[menu.FileIndex].name);
                ClearScreen();
                MovePointer (0xFF, 0xFF);

                if (!menu.FileArray[menu.FileIndex].isDir) {
                    PrintMapperMenu();
                } else {
                    // Build new folder path by appending selected dir name to current folder
                    char newFolder[256];
                    strcpy (newFolder, (char *)menu.folder);
                    size_t len = strlen (newFolder);
                    if (len > 0 && newFolder[len - 1] != '/') {
                        strcat (newFolder, "/");
                    }
                    strcat (newFolder, (char *)menu.FileArray[menu.FileIndex].name);
                    strcat (newFolder, "/");
                    strcpy ((char *)menu.folder, newFolder);
                    StoreCurrFileOffset();
                    if (menu.DirDepth < MAX_DIR_DEPTH - 1)
                        menu.DirDepth++;
                    menu.FileIndexPage = 1;
                    PrintMainMenu (menu.FileIndexPage);
                    return;
                }
            }
            // "1" change mapper
            if (key == 0x31) {
                menu.CartTypeIndex = 0;
                menu.FileIndex = 0;
                menu.FileIndexPage = 0;
                menu.pageName = CHANGEMAPPER;
                ChangeMapperMenu();
            }
            break;

        case MAPPER:
            // ESC
            if (key == 0x1B) {
                MovePointer (0xFF, 0xFF);
                PrintMainMenu (0);
            }
            // cursor up
            if (key == 0x1E) {
                if (menu.CartTypeIndex != 0) {
                    menu.CartTypeIndex--;
                    CursorUp();
                }
            }
            // cursor down
            if (key == 0x1F) {
                if (menu.CartTypeIndex != 9) {
                    menu.CartTypeIndex++;
                    CursorDown();
                }
            }
            // enter
            if (key == 0x0D) {
                ClearScreen();
                MovePointer (0xFF, 0xFF);
                appendString (&scb, " Programming file:");
                NewLine();
                append (&scb, 0x20);
                appendString (&scb, (char *)menu.Filename);
                NewLine();
                NewLine();
                appendString (&scb, " Mapper type:");
                NewLine();
                PrintMapperType (menu.CartTypeIndex);
                ProgramCart (menu.CartTypeIndex, (char *)menu.Filename, (char *)menu.folder);
                NewLine();

                appendString (&scb, "Programming FAILED!");
            }
            break;

        case CHANGEMAPPER:
            // ESC
            if (key == 0x1B) {
                MovePointer (0xFF, 0xFF);
                PrintMainMenu (0);
            }
            // cursor up
            if (key == 0x1E) {
                if (menu.CartTypeIndex != 0) {
                    menu.CartTypeIndex--;
                    CursorUp();
                }
            }
            // cursor down
            if (key == 0x1F) {
                if (menu.CartTypeIndex != 9) {
                    menu.CartTypeIndex++;
                    CursorDown();
                }
            }
            // enter
            if (key == 0x0D) {
                ClearScreen();
                MovePointer (0xFF, 0xFF);
                MapperCode_Update (menu.CartTypeIndex);
                appendString (&scb, " Rebooting ...");
                Reset();
            }
            break;

        default:
            break;
        }
    }
}

void NewLine (void) {
    append (&scb, 0x0D);
    append (&scb, 0x0A);
}

void MoveCursor (int x, int y) {
    append (&scb, 0x1B);
    append (&scb, 0x59);
    append (&scb, (0x20 + x));
    append (&scb, (0x20 + y));
}

void MovePointer (int x, int y) {
    append (&scb, 0x04);
    append (&scb, x * 8);
    append (&scb, y * 8);
}

void ClearScreen() {
    append (&scb, 0x1B);
    append (&scb, 0x45);
}

void CursorUp() {
    append (&scb, 0x1B);
    append (&scb, 0x41);
    PointerY--;
    MovePointer (PointerX, PointerY);
}

void CursorDown() {
    append (&scb, 0x1B);
    append (&scb, 0x42);
    PointerY++;
    MovePointer (PointerX, PointerY);
}

void Reset() {
    waitBufferEmpty (&scb);
    append (&scb, 0x03);
    GPIO_WriteBit (GPIOA, GPIO_Pin_8, Bit_SET);
}

void PrintMapperType (CartType type) {
    append (&scb, 0x20);
    if ((unsigned)type < (sizeof (kMapperNames) / sizeof (kMapperNames[0]))) {
        appendString (&scb, kMapperNames[type]);
    } else {
        appendString (&scb, "Mapper not recognized.");
    }
}

void PrintMapperList() {
    for (int i = 0; i < 10; ++i) {
        MoveCursor (4 + i, 2);
        appendString (&scb, kMapperNames[i]);
    }
    MoveCursor (23, 0);
}

void ResetPointer() {
    PointerX = 1;
    PointerY = 1;
}