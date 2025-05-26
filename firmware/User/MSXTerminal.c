#include "MSXTerminal.h"
#include "utils.h"
#include "cart.h"
#include "ff.h"
#include "usb_disk.h"
#include "programflash.h"


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

    appendString (&scb, "Insert USB.");

    USB_Initialization();
    while (USBH_PreDeal() != 0) { }

    FATFS fs;
    FRESULT fres;


    // Mount the filesystem
    fres = f_mount (&fs, "", 1);
    if (fres != FR_OK) {
        printf ("f_mount failed: %d\r\n", fres);
        while (1)
            ;
    }
    // auto program
    ProgramCart (ROM16k, "CART.R16", "/");
    ProgramCart (ROM32k, "CART.R32", "/");
    ProgramCart (ROM48k, "CART.R48", "/");
    ProgramCart (KonamiWithoutSCC, "CART.KO4", "/");
    ProgramCart (KonamiWithSCC, "CART.KO5", "/");
    ProgramCart (KonamiWithSCCNOSCC, "CART.KD5", "/");
    ProgramCart (ASCII8k, "CART.A8K", "/");
    ProgramCart (ASCII16k, "CART.A16", "/");
    ProgramCart (NEO16, "CART.N16", "/");
    ProgramCart (NEO8, "CART.N8K", "/");

    menu.FileIndexPage = 1;
    strcpy ((char *)menu.folder, "");
    PrintMainMenu (menu.FileIndexPage);
}

void PrintMainMenu (int page) {
    ResetPointer();
    menu.pageName = MAIN;
    menu.FileIndex = 0;
    ClearScreen();
    menu.FileIndexSize = listFiles (menu.folder, menu.FileArray, page);

    appendString (&scb, " v2.1.8   RISKY MSX ");
    appendString (&scb, "Page:");
    char pageString[5];
    intToString (page, pageString);
    appendString (&scb, pageString);
    NewLine();

    for (int i = 0; i < menu.FileIndexSize; i++) {
        MoveCursor (i + 1, 2);
        if (menu.FileArray[i]->isDir) {
            appendString (&scb, "<DIR> ");
        } else {
            uint32_t sizeAdjusted = 0;
            uint8_t FileNameSize[5] = {0x20, 0x20, 0x20, 0x20, 0x20};
            uint32_t FileSize = menu.FileArray[i]->size_kb;
            if (FileSize >= 1073741824) {  // 1 GB = 1073741824 bytes
                sizeAdjusted = FileSize / 1073741824;
                intToString (sizeAdjusted, (char *)FileNameSize);
                FileNameSize[3] = 0x47;        // G
                FileNameSize[4] = 0x42;        // B
            } else if (FileSize >= 1048576) {  // 1 MB = 1048576 bytes
                sizeAdjusted = FileSize / 1048576;
                intToString (sizeAdjusted, (char *)FileNameSize);
                FileNameSize[3] = 0x4D;     // M
                FileNameSize[4] = 0x42;     // B
            } else if (FileSize >= 1024) {  // 1 KB = 1024 bytes
                sizeAdjusted = FileSize / 1024;
                intToString (sizeAdjusted, (char *)FileNameSize);
                FileNameSize[3] = 0x4B;  // K
                FileNameSize[4] = 0x42;  // B
            } else {
                intToString (FileSize, (char *)FileNameSize);
                FileNameSize[4] = 0x42;  // B
            }

            for (int x = 0; x < 5; x++) {
                append (&scb, FileNameSize[x]);
            }
            append (&scb, 0x20);
        }


        appendString (&scb, menu.FileArray[i]->name);


        NewLine();
    }

    MoveCursor (22, 0);
    appendString (&scb, " Folder:");

    uint8_t *location = (uint8_t *)malloc (64 * sizeof (uint8_t));
    strcpy ((char *)location, (char *)menu.folder);
    int length = strlen ((char *)location);
    if (length > 0) {
        location[length - 1] = '\0';
    }
    appendString (&scb, (char *)location);
    free (location);

    MoveCursor (23, 0);
    appendString (&scb, " ARROWS,RETURN,ESC,1-MAPPER 9");


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
    strcpy ((char *)location, (char *)menu.Filename);
    append (&scb, 0x20);
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
        if (key == 0x1B) {

            ClearScreen();
            // appendString (&scb, "Insert USB.");
            menu.FileIndexPage = 1;
            strcpy ((char *)menu.folder, "/");
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
                    menu.FileIndexPage = 1;
                    ClearScreen();
                    handle_path ((char *)menu.Filename);
                    strcat ((char *)menu.Filename, "/");
                    strcpy ((char *)menu.folder, (char *)menu.Filename);
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
                //  MapperCode_Update (menu.CartTypeIndex);
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