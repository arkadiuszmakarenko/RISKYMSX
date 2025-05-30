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
uint32_t ShortNames;
uint32_t volatile enableTerminal = 0;

int PointerX = 1;
int PointerY = 1;

void Init_MSXTerminal (void) {
    initBuffer (&scb);
    initMiniBuffer (&icb);


    while (enableTerminal == 0) { };

    menu.Filename = (uint8_t *)malloc (255 * sizeof (uint8_t));
    menu.folder = (uint8_t *)malloc (255 * sizeof (uint8_t));

    for (int i = 0; i < 20; i++) {
        menu.FileArray[i] = (FileEntry *)malloc (sizeof (FileEntry));
    }
    ClearScreen();

    GPIO_WriteBit (GPIOA, GPIO_Pin_0, Bit_RESET);
    GPIO_WriteBit (GPIOA, GPIO_Pin_1, Bit_RESET);
    GPIO_WriteBit (GPIOA, GPIO_Pin_2, Bit_RESET);
    GPIO_WriteBit (GPIOA, GPIO_Pin_3, Bit_RESET);

    GPIO_WriteBit (GPIOA, GPIO_Pin_8, Bit_RESET);

    appendString (&scb, " ");
    appendString (&scb, FIRMWARE_VERSION_STRING);
    NewLine();
    USB_Initialization();
    appendString (&scb, "Insert USB.");
    NewLine();
    NewLine();

    FATFS fs;
    FRESULT fres;
    FILINFO fno;
    // Mount the filesystem
    do {
        fres = f_mount (&fs, "", 1);
        if (fres != FR_OK) {
            Delay_Ms (500);
        }
    } while (fres != FR_OK);

    // auto program
    fres = f_stat ("CART.R16", &fno);
    if (fres == FR_OK) {
        ProgramCart (ROM16k, "CART.R16", "/");
    }

    fres = f_stat ("CART.R32", &fno);
    if (fres == FR_OK) {
        ProgramCart (ROM32k, "CART.R32", "/");
    }

    fres = f_stat ("CART.R48", &fno);
    if (fres == FR_OK) {
        ProgramCart (ROM48k, "CART.R48", "/");
    }

    fres = f_stat ("CART.KO4", &fno);
    if (fres == FR_OK) {
        ProgramCart (KonamiWithoutSCC, "CART.KO4", "/");
    }

    fres = f_stat ("CART.KO5", &fno);
    if (fres == FR_OK) {
        ProgramCart (KonamiWithSCC, "CART.KO5", "/");
    }

    fres = f_stat ("CART.KD5", &fno);
    if (fres == FR_OK) {
        ProgramCart (KonamiWithSCCNOSCC, "CART.KD5", "/");
    }

    fres = f_stat ("CART.A8K", &fno);
    if (fres == FR_OK) {
        ProgramCart (ASCII8k, "CART.A8K", "/");
    }

    fres = f_stat ("CART.A16", &fno);
    if (fres == FR_OK) {
        ProgramCart (ASCII16k, "CART.A16", "/");
    }

    fres = f_stat ("CART.N16", &fno);
    if (fres == FR_OK) {
        ProgramCart (NEO16, "CART.N16", "/");
    }

    fres = f_stat ("CART.N8K", &fno);
    if (fres == FR_OK) {
        ProgramCart (NEO8, "CART.N8K", "/");
    }

    menu.FileIndexPage = 1;
    strcpy ((char *)menu.folder, "");
    PrintMainMenu (menu.FileIndexPage);
}

void PrintMainMenu (int page) {
    uint32_t sizeAdjusted = 0;
    uint8_t FileNameSize[5] = {0x20, 0x20, 0x20, 0x20, 0x20};

    ClearScreen();
    ResetPointer();
    menu.pageName = MAIN;
    menu.FileIndex = 0;
    ClearScreen();
    menu.FileIndexSize = listFiles (menu.folder, menu.FileArray, page);


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
            char c = menu.FileArray[i]->name[x];
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

        if (menu.FileArray[i]->isDir) {
            appendString (&scb, "<DIR> ");
        } else {
            uint32_t FileSize = menu.FileArray[i]->size_kb;
            if (FileSize >= 1073741824) {
                sizeAdjusted = FileSize / 1073741824;
                intToString (sizeAdjusted, (char *)FileNameSize);
                FileNameSize[3] = 0x47;  // G
                FileNameSize[4] = 0x42;  // B
            } else if (FileSize >= 1048576) {
                sizeAdjusted = FileSize / 1048576;
                intToString (sizeAdjusted, (char *)FileNameSize);
                FileNameSize[3] = 0x4D;  // M
                FileNameSize[4] = 0x42;  // B
            } else if (FileSize >= 1024) {
                sizeAdjusted = FileSize / 1024;
                intToString (sizeAdjusted, (char *)FileNameSize);
                FileNameSize[3] = 0x4B;  // K
                FileNameSize[4] = 0x42;  // B
            } else {
                intToString (FileSize, (char *)FileNameSize);
                FileNameSize[4] = 0x42;  // B
            }
            // Print 5-char size
            for (int x = 0; x < 5; x++) {
                append (&scb, FileNameSize[x]);
            }
            append (&scb, 0x20);
        }
        NewLine();
    }

    MoveCursor (21, 0);
    uint8_t *location = (uint8_t *)malloc (64 * sizeof (uint8_t));
    strncpy ((char *)location, (char *)menu.folder, 63);
    location[63] = '\0';
    appendString (&scb, (char *)location);
    free (location);

    MoveCursor (23, 0);
    appendString (&scb, " ARROWS,RET,ESC,1-MAP,BKSP-..");

    MoveCursor (1, 1);
    MovePointer (PointerX, PointerY);
}

void PrintMapperMenu() {
    ResetPointer();
    menu.pageName = MAPPER;
    ClearScreen();
    appendString (&scb, "          RISKY MSX ");
    NewLine();
    appendString (&scb, "  File to flash:");
    NewLine();
    append (&scb, 0x20);
    append (&scb, 0x20);
    appendString (&scb, (char *)menu.Filename);
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
    ResetPointer();
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
        if (key == 0x1B) {  // handle ESC

            ClearScreen();
            appendString (&scb, "Insert USB.");
            menu.FileIndexPage = 1;
            strcpy ((char *)menu.folder, "");
            FRESULT fres;
            FATFS fs;
            do {
                fres = f_mount (&fs, "", 1);
                if (fres != FR_OK) {
                    Delay_Ms (500);
                }
            } while (fres != FR_OK);

            PrintMainMenu (menu.FileIndexPage);
        }

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
            menu.FileIndexPage = 1;
            ClearScreen();
            PrintMainMenu (menu.FileIndexPage);
        }

        switch (menu.pageName) {
        case MAIN:
            if (key == 0x1C) {
                if (menu.FileIndexSize < 20)
                    return;
                menu.FileIndexPage++;
                PrintMainMenu (menu.FileIndexPage);
            }
            if (key == 0x1D) {
                if (menu.FileIndexPage == 1)
                    return;
                menu.FileIndexPage--;
                PrintMainMenu (menu.FileIndexPage);
            }

            if (key == 0x1E) {
                if (menu.FileIndex != 0) {
                    menu.FileIndex--;
                    CursorUp();
                }
            }

            if (key == 0x1F) {
                if (menu.FileIndex == (menu.FileIndexSize - 1))
                    return;
                menu.FileIndex++;
                CursorDown();
            }

            if (key == 0x0D) {
                menu.CartTypeIndex = 0;
                strcpy ((char *)menu.Filename, (char *)menu.FileArray[menu.FileIndex]->name);
                ClearScreen();

                if (!menu.FileArray[menu.FileIndex]->isDir) {
                    PrintMapperMenu();
                } else {
                    // Build new folder path by appending selected dir name to current folder
                    char newFolder[256];
                    strcpy (newFolder, (char *)menu.folder);
                    size_t len = strlen (newFolder);
                    if (len > 0 && newFolder[len - 1] != '/') {
                        strcat (newFolder, "/");
                    }
                    strcat (newFolder, (char *)menu.FileArray[menu.FileIndex]->name);
                    strcat (newFolder, "/");
                    strcpy ((char *)menu.folder, newFolder);
                    menu.FileIndexPage = 1;
                    ClearScreen();
                    PrintMainMenu (menu.FileIndexPage);
                    return;
                }
            }

            if (key == 0x31) {
                menu.CartTypeIndex = 0;
                menu.FileIndex = 0;
                menu.FileIndexPage = 0;
                menu.pageName = CHANGEMAPPER;
                ChangeMapperMenu();
            }


            break;

        case MAPPER:

            if (key == 0x1E) {
                if (menu.CartTypeIndex != 0) {
                    menu.CartTypeIndex--;
                    CursorUp();
                }
            }

            if (key == 0x1F) {
                if (menu.CartTypeIndex != 9) {
                    menu.CartTypeIndex++;
                    CursorDown();
                }
            }

            if (key == 0x0D) {
                ClearScreen();
                MovePointer (0xFF, 0xFF);
                appendString (&scb, " Programming file:");
                NewLine();
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
            if (key == 0x1E) {
                if (menu.CartTypeIndex != 0) {
                    menu.CartTypeIndex--;
                    CursorUp();
                }
            }

            if (key == 0x1F) {
                if (menu.CartTypeIndex != 9) {
                    menu.CartTypeIndex++;
                    CursorDown();
                }
            }

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
    append (&scb, 0x03);
    GPIO_WriteBit (GPIOA, GPIO_Pin_8, Bit_SET);
}

void PrintMapperType (CartType type) {
    append (&scb, 0x20);
    switch (type) {
    case ROM16k:
        appendString (&scb, "Standard ROM 16k");
        break;
    case ROM32k:
        appendString (&scb, "Standard ROM 32k");
        break;
    case ROM48k:
        appendString (&scb, "Standard ROM 48k");
        break;
    case KonamiWithoutSCC:
        appendString (&scb, "Konami without SCC");
        break;
    case KonamiWithSCC:
        appendString (&scb, "Konami With SCC (EN)");
        break;
    case KonamiWithSCCNOSCC:
        appendString (&scb, "Konami With SCC (DIS)");
        break;
    case ASCII8k:
        appendString (&scb, "ASCII 8k");
        break;
    case ASCII16k:
        appendString (&scb, "ASCII 16k");
        break;
    case NEO8:
        appendString (&scb, "Neo 8k");
        break;
    case NEO16:
        appendString (&scb, "Neo 16k");
        break;
    default:
        appendString (&scb, "Mapper not recognized.");
        break;
    }
}

void PrintMapperList() {
    MoveCursor (4, 2);
    appendString (&scb, "Standard 16KB ROM");
    MoveCursor (5, 2);
    appendString (&scb, "Standard 32KB ROM");
    MoveCursor (6, 2);
    appendString (&scb, "Standard 48KB or 64KB ROM");
    MoveCursor (7, 2);
    appendString (&scb, "KONAMI without SCC");
    MoveCursor (8, 2);
    appendString (&scb, "KONAMI with SCC (EN)");
    MoveCursor (9, 2);
    appendString (&scb, "KONAMI with SCC (DIS)");
    MoveCursor (10, 2);
    appendString (&scb, "ASCII 8KB");
    MoveCursor (11, 2);
    appendString (&scb, "ASCII 16KB");
    MoveCursor (12, 2);
    appendString (&scb, "NEO 8KB");
    MoveCursor (13, 2);
    appendString (&scb, "NEO 16KB");

    MoveCursor (23, 0);
}

void ResetPointer() {
    PointerX = 1;
    PointerY = 1;
}