#include "msxbus.h"
#include "termui.h"
#include "appflash.h"
#include "usb_disk.h"
#include "usb_host_config.h"
#include "ff.h"
#include "debug.h"
#include "boot_debug.h"
#include "ch32v30x.h"
#include <string.h>

/*
 * RISKYMSX system bootloader.
 *
 * The bootloader lives in the 28 KiB system boot flash and runs when BOOT0 is
 * high. It serves the MSX cartridge terminal, looks for a firmware image on
 * an attached USB stick, programs it into the main flash and reports the
 * progress and the outcome. The application is started the usual way, with
 * BOOT0 low, so the bootloader never has to relocate the application image.
 *
 * The bootloader-specific MSX terminal is served at address 0x4000 and talks
 * to this firmware through #7FFD (input), #7FFE (mode) and #7FFF (output).
 */

/* Candidate image names on the USB stick, checked in order. */
static const char *const image_candidates[] = {
    "RISKYMSXCART.BIN",
    "FIRMWARE.BIN",
    "UPDATE.BIN",
};

#define CANDIDATE_COUNT (sizeof(image_candidates) / sizeof(image_candidates[0]))
#define BOOTLOADER_VERSION "0.1.0"

static FATFS fs;

static void draw_banner(void)
{
    TermUi_Clear();
    TermUi_MoveCursor(0, 0);
    TermUi_Write("RISKYMSX Bootloader v" BOOTLOADER_VERSION);
    TermUi_NewLine();
    TermUi_NewLine();
}

/* Returns the size of the first usable image, -1 if none, or -2 on I/O error. */
static int find_image(char *chosen, size_t chosen_size)
{
    for (size_t i = 0; i < CANDIDATE_COUNT; i++) {
        FIL file;

        FRESULT result = f_open(&file, (const TCHAR *)image_candidates[i], FA_READ);
        if (result == FR_NO_FILE || result == FR_NO_PATH) {
            TermUi_Write("Not found: ");
            TermUi_Write(image_candidates[i]);
            TermUi_NewLine();
            continue;
        }
        if (result != FR_OK) {
            TermUi_Write("File read error: ");
            TermUi_WriteNumber((uint32_t)result);
            TermUi_NewLine();
            return -2;
        }

        uint32_t size = f_size(&file);
        f_close(&file);

        if (size == 0) {
            TermUi_Write("Empty: ");
            TermUi_Write(image_candidates[i]);
            TermUi_NewLine();
            continue;
        }

        TermUi_Write("Found: ");
        TermUi_Write(image_candidates[i]);
        TermUi_NewLine();

        strncpy(chosen, image_candidates[i], chosen_size - 1);
        chosen[chosen_size - 1] = '\0';
        return (int)size;
    }
    return -1;
}

static void progress_callback(uint8_t percent)
{
    static int programming_shown;
    TermUi_Progress(percent);
    if (percent == 100) {
        TermUi_WriteAt(6, 13, "Verifying...  ");
        TermUi_NewLine();
    } else if (!programming_shown) {
        programming_shown = 1;
        TermUi_WriteAt(6, 13, "Programming...");
        TermUi_NewLine();
    }
    TermUi_Flush();
    Delay_Ms(20);
}

/* Waits for the user to press Return, or gives up after a timeout. */
static int wait_for_confirm(uint32_t timeout_ms)
{
    uint32_t waited = 0;

    while (waited < timeout_ms) {
        uint8_t key;
        if (MsxBus_TakeInput(&key)) {
            if (key == 0x0D || key == 0x0A) {
                return 1;
            }
            if (key == 0x1B) {
                return 0;
            }
        }
        Delay_Ms(10);
        waited += 10;
    }
    return 0;
}

static void report_usb(uint8_t status)
{
    switch (status) {
    case DEF_SUCCESS:
        TermUi_Write("USB drive: found");
        break;
    case DEF_ERR_DETECT:
        TermUi_Write("USB drive: not attached");
        break;
    default:
        TermUi_Write("USB drive: error, cannot read");
        break;
    }
    TermUi_NewLine();
}

/* Reports what the programmer found on the USB bus. */
static void report_usb_details(void)
{
    const UsbDeviceInfo *info = UsbDisk_GetDeviceInfo();

    if (!info->enumerated) {
        return;
    }

    TermUi_Write("  Speed:");
    switch (info->speed) {
    case USB_LOW_SPEED:
        TermUi_Write("low");
        break;
    case USB_FULL_SPEED:
        TermUi_Write("full");
        break;
    case USB_HIGH_SPEED:
        TermUi_Write("high");
        break;
    default:
        TermUi_Write("unknown");
        break;
    }
    TermUi_NewLine();

    TermUi_Write("  Address:");
    TermUi_WriteNumber(info->device_address);
    TermUi_NewLine();

    TermUi_Write("  Class:");
    TermUi_WriteNumber(info->device_class);
    TermUi_Write("  USB 2.");
    TermUi_WriteNumber(info->usb_version & 0x0F);
    TermUi_NewLine();

    TermUi_Write("  Endpoints: IN");
    TermUi_WriteNumber(info->bulk_in_ep);
    TermUi_Write(" OUT");
    TermUi_WriteNumber(info->bulk_out_ep);
    TermUi_NewLine();

    if (info->capacity_known) {
        TermUi_Write("  Capacity:");
        TermUi_WriteSize(info->block_count * info->block_size);
        TermUi_NewLine();
    }
}

int main(void)
{
    /* Trim the HSI oscillator. The USB host needs an accurate 48 MHz clock
       derived from the PLL, and the HSI is the only clock source on this
       board. The factory trim value of 0x1F is what the cartridge firmware
       uses as well. */
    RCC_AdjustHSICalibrationValue(0x1F);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    SystemCoreClockUpdate();
    Delay_Init();

    /* The bootloader executes from the system boot flash while the main flash
       is used for erase and program operations. The reference manual states
       that erase and program operations fail while the flash enhanced read
       mode is active, so the mode is left off. The hardware already clears
       the mode after a reset; the programming routines in appflash.c disable
       it again right before they touch the flash. */

#if BOOTLOADER_DEBUG
    USART_Printf_Init(115200);
    printf("RISKYMSX bootloader\r\n");
    printf("SystemClk:%d\r\n", SystemCoreClock);
#endif

    MsxBus_Init();

    /* The MSX side program selects the terminal by writing zero to #7FFE.
       It also asks for the cartridge ROM by writing a non-zero value, which
       the bus routine turns into a reset with the handover flag set. */
    while (!msx_terminal_requested) {
    }

    draw_banner();

    TermUi_Write("Mounting USB drive...");
    TermUi_NewLine();
    TermUi_Flush();

    /* The USB host port is fed through the CH217K current limited switch.
       Its EN# input is active low, so the port stays unpowered until PB12 is
       driven low. Without this the switch is off and no device can be seen
       on the bus. */
    Vbus_Init();
    Vbus_Enable();
    Delay_Ms(50);

    USB_Initialization();
    uint8_t usb_status = USB_TryEnsureEnumerated();
    report_usb(usb_status);
    TermUi_Flush();

    if (Vbus_Faulted()) {
        TermUi_Info("Mount failed.");
        TermUi_Info("Firmware: not checked.");
        TermUi_Error("USB port power fault.");
        TermUi_Info("Disconnect drive and reset.");
        TermUi_Flush();
        while (1) {
        }
    }

    if (usb_status != DEF_SUCCESS) {
        TermUi_Info("Mount failed.");
        TermUi_Info("Firmware: not checked.");
        TermUi_Error("USB drive unavailable.");
        TermUi_Info("Attach FAT drive and reset.");
        TermUi_Flush();
        while (1) {
        }
    }

    report_usb_details();
    TermUi_Flush();

    FRESULT fres = f_mount(&fs, "", 1);
    if (fres != FR_OK) {
        TermUi_Write("Mount failed (FatFs ");
        TermUi_WriteNumber((uint32_t)fres);
        TermUi_Write(").");
        TermUi_NewLine();
        TermUi_Info("Firmware: not checked.");
        TermUi_Info("Use FAT16/32 and reset.");
        TermUi_Flush();
        while (1) {
        }
    }

    TermUi_Write("Drive mounted successfully.");
    TermUi_NewLine();
    TermUi_NewLine();

    char image_name[32];
    int image_size = find_image(image_name, sizeof(image_name));

    if (image_size == -2) {
        TermUi_Error("Cannot search drive.");
        TermUi_Info("Check drive and reset.");
        TermUi_Flush();
        while (1) {
        }
    }
    if (image_size < 0) {
        TermUi_NewLine();
        TermUi_Error("No firmware found.");
        TermUi_Info("Copy RISKYMSXCART.BIN to");
        TermUi_Info("drive root, then reset.");
        TermUi_Flush();
        while (1) {
        }
    }

    TermUi_Write("Size: ");
    TermUi_WriteSize((uint32_t)image_size);
    TermUi_NewLine();
    TermUi_NewLine();
    TermUi_Write("Enter: program");
    TermUi_NewLine();
    TermUi_Write("Esc: cancel (30s timeout)");
    TermUi_NewLine();
    TermUi_Flush();

    int confirmed = wait_for_confirm(30000);
    TermUi_Clear();
    TermUi_MoveCursor(0, 0);

    if (!confirmed) {
        TermUi_Write("Cancelled. Nothing was changed.");
        TermUi_NewLine();
        TermUi_Flush();
        while (1) {
        }
    }

    TermUi_WriteAt(7, 9, "Updating firmware");
    TermUi_NewLine();
    TermUi_WriteAt(6, 13, "Erasing...");
    TermUi_NewLine();
    TermUi_Progress(0);
    TermUi_Flush();
    Delay_Ms(20);

    const char *detail = NULL;
    AppFlashResult result = AppFlash_Program(image_name, progress_callback, &detail);

    if (result == APP_FLASH_OK) {
        TermUi_WriteAt(6, 13, "Verified.       ");
        TermUi_NewLine();
        TermUi_WriteAt(2, 16, "Open B0 and reset to run.");
        TermUi_NewLine();
    } else {
        TermUi_WriteAt(2, 13, "Update failed.               ");
        TermUi_NewLine();
        TermUi_MoveCursor(2, 15);
        TermUi_Error(AppFlash_ResultText(result));
        TermUi_WriteAt(2, 17, "Firmware may be damaged.");
        TermUi_NewLine();
        TermUi_WriteAt(2, 18, "Check drive and retry.");
        TermUi_NewLine();
    }
    TermUi_Flush();

    while (1) {
    }
}
