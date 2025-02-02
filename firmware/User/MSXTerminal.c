#include "MSXTerminal.h"
#include "utils.h"
#include "cart.h"
#include "usb_host_iap.h"

typedef struct TerminalPageState {
    uint32_t page_usb;
    MenuType pageName;
    uint32_t FileIndex;
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
    appendString (&scb, "Insert USB.");
    while (MountDrive() == 0) { };
    PrintMainMenu (0);
    menu.pageName = MAIN;
    menu.page_usb = 0;
    menu.FileIndex = 0;
}

void PrintMainMenu (int page) {
    append (&scb, 0x1B);
    append (&scb, 0x45);
    appendString (&scb, "          RISKY MSX ");
    appendString (&scb, "Page:");
    char pageString[5];
    intToString (page, pageString);
    appendString (&scb, pageString);
    append (&scb, 0x0D);
    append (&scb, 0x0A);
    uint32_t size = 0;

    size = listFiles (FileList, page);

    for (int i = 0; i < size; i++) {
        printFilename (FileList[i], 55);
    }
    append (&scb, 0x1B);
    append (&scb, 0x79);
    append (&scb, 0x35);

    // 1Bh + 59h + x + y
    append (&scb, 0x1B);
    append (&scb, 0x59);
    append (&scb, 0x21);
    append (&scb, 0x20);
}

void PrintMapperMenu() {
    append (&scb, 0x1B);
    append (&scb, 0x45);
    appendString (&scb, "          RISKY MSX ");
    append (&scb, 0x0D);
    append (&scb, 0x0A);

    appendString (&scb, "File to flash:");
    append (&scb, 0x0D);
    append (&scb, 0x0A);
    printFilename (FileList[menu.FileIndex], 40);
    append (&scb, 0x0D);
    append (&scb, 0x0A);

    appendString (&scb, " Standard 32KB ROM");
    append (&scb, 0x0D);
    append (&scb, 0x0A);
    appendString (&scb, " Standard 48KB or 64KB ROM");
    append (&scb, 0x0D);
    append (&scb, 0x0A);
    appendString (&scb, " KONAMI without SCC");
    append (&scb, 0x0D);
    append (&scb, 0x0A);
    appendString (&scb, " KONAMI with SCC - SCC ");
    append (&scb, 0x0D);
    append (&scb, 0x0A);
    appendString (&scb, " KONAMI with SCC - NO SCC");
    append (&scb, 0x0D);
    append (&scb, 0x0A);
    appendString (&scb, " ASCII 8KB");
    append (&scb, 0x0D);
    append (&scb, 0x0A);
    appendString (&scb, " ASCII 16KB");
    append (&scb, 0x0D);
    append (&scb, 0x0A);
    appendString (&scb, " NEO 8KB");
    append (&scb, 0x0D);
    append (&scb, 0x0A);
    appendString (&scb, " NEO 16KB");
    append (&scb, 0x0D);
    append (&scb, 0x0A);

    append (&scb, 0x1B);
    append (&scb, 0x59);
    append (&scb, 0x24);
    append (&scb, 0x20);
}

void ProcessMSXTerminal (void) {
    uint32_t key;
    if (popmini (&icb, &key) == 0) {

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
                    append (&scb, 0x1B);
                    append (&scb, 0x41);
                }
            }

            if (key == 0x1F) {
                if (menu.FileIndex != 19) {
                    menu.FileIndex++;
                    append (&scb, 0x1B);
                    append (&scb, 0x42);
                }
            }

            if (key == 0x0D) {
                menu.pageName = MAPPER;
                PrintMapperMenu();
            }

            break;

        case MAPPER:

            if (key == 0x1E) {
                if (menu.CartTypeIndex != 0) {
                    menu.CartTypeIndex--;
                    append (&scb, 0x1B);
                    append (&scb, 0x41);
                }
            }

            if (key == 0x1F) {
                if (menu.CartTypeIndex != 8) {
                    menu.CartTypeIndex++;
                    append (&scb, 0x1B);
                    append (&scb, 0x42);
                }
            }

            if (key == 0x0D) {
                ProgramCart (FileList[menu.FileIndex], menu.CartTypeIndex);
            }

            break;

        default:
            break;
        }
    }
}