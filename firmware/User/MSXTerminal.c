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

void Init_MSXTerminal (void) {
    initBuffer (&scb);
    initMiniBuffer (&icb);
    IAP_Initialization();
    ClearScreen();
    appendString (&scb, "Insert USB.");
    while (MountDrive() == 0) { };
    menu.page_usb = 0;
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

    menu.FileIndexSize = listFiles (FileList, page);
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
        printFilename (FileList[i]);
    }
    MoveCursor (23, 0);
    appendString (&scb, " UP,DOWN,LEFT,RIGHT,RETURN,ESC");


    // Show cursor
    append (&scb, 0x1B);
    append (&scb, 0x79);
    append (&scb, 0x35);

    MoveCursor (1, 0);
}

void PrintMapperMenu() {
    menu.pageName = MAPPER;
    ClearScreen();
    appendString (&scb, "          RISKY MSX ");
    NewLine();
    appendString (&scb, "File to flash:");
    NewLine();
    printFilename (FileList[menu.FileIndex]);
    NewLine();
    appendString (&scb, " Standard 32KB ROM");
    NewLine();
    appendString (&scb, " Standard 48KB or 64KB ROM");
    NewLine();
    appendString (&scb, " KONAMI without SCC");
    NewLine();
    appendString (&scb, " KONAMI with SCC (EN)");
    NewLine();
    appendString (&scb, " KONAMI with SCC (DIS)");
    NewLine();
    appendString (&scb, " ASCII 8KB");
    NewLine();
    appendString (&scb, " ASCII 16KB");
    NewLine();
    appendString (&scb, " NEO 8KB");
    NewLine();
    appendString (&scb, " NEO 16KB");
    NewLine();
    MoveCursor (23, 0);
    appendString (&scb, " UP,DOWN,RETURN,ESC ");
    MoveCursor (4, 0);
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
                PrintMapperMenu();
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
                appendString (&scb, "Programming file:");
                NewLine();
                printFilename (FileList[menu.FileIndex]);
                NewLine();
                appendString (&scb, "Mapper type:");
                NewLine();
                switch (menu.CartTypeIndex) {
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
                case NEO16:
                    appendString (&scb, "Neo 16k");
                    break;
                case NEO8:
                    appendString (&scb, "Neo 8k");
                    break;
                }
                ProgramCart (FileList[menu.FileIndex], menu.CartTypeIndex);
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