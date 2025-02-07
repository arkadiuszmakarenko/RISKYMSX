#include "MSXTerminal.h"
#include "utils.h"
#include "cart.h"
#include "usb_host_iap.h"

typedef struct TerminalPageState {
    uint32_t page_usb;
    MenuType pageName;
    uint32_t FileIndex;
    uint32_t FileIndexSize;
    uint32_t CartTypeIndex;
} MenuState;

CircularBuffer scb;
CircularBuffer icb;
MenuState menu;
int FileList[20];
char folder[12];

void Init_MSXTerminal (void) {
    initBuffer (&scb);
    initMiniBuffer (&icb);
    IAP_Initialization();
    ClearScreen();
    appendString (&scb, "Insert USB.");
    while (MountDrive() == 0) { };

    // auto program
    ProgramCart (0, ROM32k, 0, "/CART.R32");
    ProgramCart (0, ROM48k, 0, "/CART.R48");
    ProgramCart (0, KonamiWithoutSCC, 0, "/CART.KO4");
    ProgramCart (0, KonamiWithSCC, 0, "/CART.KO5");
    ProgramCart (0, KonamiWithSCCNOSCC, 0, "/CART.KD5");
    ProgramCart (0, ASCII8k, 0, "/CART.A8K");
    ProgramCart (0, ASCII16k, 0, "/CART.A16");
    ProgramCart (0, NEO16, 0, "/CART.N16");
    ProgramCart (0, NEO8, 0, "/CART.N8K");
    menu.page_usb = 0;
    strcpy (folder, "/*");
    PrintMainMenu (0);
}

void PrintMainMenu (int page) {
    menu.pageName = MAIN;
    menu.FileIndex = 0;
    menu.FileIndexSize = 0;
    ClearScreen();
    if (CHRV3DiskConnect() != ERR_SUCCESS) {
        appendString (&scb, "Insert USB.");
        while (CHRV3DiskConnect() == ERR_USB_DISCON) { };
    }

    menu.FileIndexSize = listFiles (FileList, page, folder);
    if (menu.FileIndexSize == 0) {
        while (MountDrive() == 0) { };
        menu.page_usb = 0;
        PrintMainMenu (0);
        return;
    }
    ClearScreen();
    appendString (&scb, "          RISKY MSX ");
    appendString (&scb, "Page:");
    char pageString[5];
    intToString (page, pageString);
    appendString (&scb, pageString);
    NewLine();


    for (int i = 0; i < menu.FileIndexSize; i++) {
        MoveCursor (i + 1, 1);
        printFilename (FileList[i], folder);
    }
    MoveCursor (23, 0);
    appendString (&scb, " ARROWS,RETURN,ESC,1");


    // Show cursor
    append (&scb, 0x1B);
    append (&scb, 0x79);
    append (&scb, 0x35);

    MoveCursor (1, 1);
}

void PrintMapperMenu() {
    menu.pageName = MAPPER;
    ClearScreen();
    appendString (&scb, "          RISKY MSX ");
    NewLine();
    appendString (&scb, "  File to flash:");
    NewLine();
    append (&scb, 0x20);
    printFilename (FileList[menu.FileIndex], folder);
    NewLine();
    PrintMapperList();

    MoveCursor (23, 0);
    appendString (&scb, " UP,DOWN,RETURN,ESC ");
    MoveCursor (4, 1);
}

void ChangeMapperMenu() {
    CART_CFG volatile *cfg;
    // Read config from config flash location.
    uint8_t *restrict cfgpnt = (uint8_t *)&__cfg_section_start;
    cfg = (CART_CFG volatile *)cfgpnt;
    CartType volatile type;
    type = cfg->CartType;

    ClearScreen();
    appendString (&scb, "Filename flashed:");
    MoveCursor (1, 0);
    for (int j = 0; j != 60; j++) {
        append (&scb, *(cfgpnt + 8 + j));
    }
    MoveCursor (2, 0);
    PrintMapperType (type);
    MoveCursor (3, 0);
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
            appendString (&scb, "Insert USB.");
            while (CHRV3DiskConnect() == ERR_USB_DISCON) { };
            while (MountDrive() == 0) { };
            menu.page_usb = 0;
            strcpy (folder, "/*");
            PrintMainMenu (0);
        }

        switch (menu.pageName) {
        case MAIN:
            if (key == 0x1C) {
                menu.page_usb++;
                menu.FileIndex = 0;
                PrintMainMenu (menu.page_usb);
            }
            if (key == 0x1D) {
                if (menu.page_usb == 0)
                    return;
                menu.page_usb--;
                menu.FileIndex = 0;
                PrintMainMenu (menu.page_usb);
            }

            if (key == 0x1E) {
                if (menu.FileIndex != 0) {
                    menu.FileIndex--;
                    CursorUp();
                }
            }

            if (key == 0x1F) {
                if (menu.FileIndex != (menu.FileIndexSize - 1)) {
                    menu.FileIndex++;
                    CursorDown();
                }
            }

            if (key == 0x0D) {
                menu.CartTypeIndex = 0;
                if (isFile (menu.FileIndex, folder) != 0) {
                    ClearScreen();
                    appendString (&scb, " NO FOLDER SUPPORT!");
                    Delay_Ms (1000);
                    menu.page_usb = 0;
                    strcpy (folder, "/*");
                    PrintMainMenu (0);
                    return;
                }
                PrintMapperMenu();
            }

            if (key == 0x31) {
                menu.CartTypeIndex = 0;
                menu.FileIndex = 0;
                menu.page_usb = 0;
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
                if (menu.CartTypeIndex != 8) {
                    menu.CartTypeIndex++;
                    CursorDown();
                }
            }

            if (key == 0x0D) {
                ClearScreen();
                appendString (&scb, " Programming file:");
                NewLine();

                printFilename (FileList[menu.FileIndex], folder);
                NewLine();
                NewLine();
                appendString (&scb, " Mapper type:");
                NewLine();
                PrintMapperType (menu.CartTypeIndex);
                ProgramCart (FileList[menu.FileIndex], menu.CartTypeIndex, 1, "/*");
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
                if (menu.CartTypeIndex != 8) {
                    menu.CartTypeIndex++;
                    CursorDown();
                }
            }

            if (key == 0x0D) {
                ClearScreen();
                MapperCode_Update (menu.CartTypeIndex);
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

void ClearScreen() {
    append (&scb, 0x1B);
    append (&scb, 0x45);
}

void CursorUp() {
    append (&scb, 0x1B);
    append (&scb, 0x41);
}

void CursorDown() {
    append (&scb, 0x1B);
    append (&scb, 0x42);
}

void Reset() {

    append (&scb, 0x03);
    append (&scb, 0x03);
    Delay_Ms (100);
    PFIC->SCTLR |= (1 << 31);
}

void PrintMapperType (CartType type) {
    append (&scb, 0x20);
    switch (type) {
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
    appendString (&scb, "Standard 32KB ROM");
    MoveCursor (5, 2);
    appendString (&scb, "Standard 48KB or 64KB ROM");
    MoveCursor (6, 2);
    appendString (&scb, "KONAMI without SCC");
    MoveCursor (7, 2);
    appendString (&scb, "KONAMI with SCC (EN)");
    MoveCursor (8, 2);
    appendString (&scb, "KONAMI with SCC (DIS)");
    MoveCursor (9, 2);
    appendString (&scb, "ASCII 8KB");
    MoveCursor (10, 2);
    appendString (&scb, "ASCII 16KB");
    MoveCursor (11, 2);
    appendString (&scb, "NEO 8KB");
    MoveCursor (12, 2);
    appendString (&scb, "NEO 16KB");

    MoveCursor (23, 0);
}