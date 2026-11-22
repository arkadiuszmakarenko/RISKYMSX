#ifndef BOOT_DEBUG_H
#define BOOT_DEBUG_H

#ifndef BOOTLOADER_DEBUG
#define BOOTLOADER_DEBUG 0
#endif

#if BOOTLOADER_DEBUG != 0 && BOOTLOADER_DEBUG != 1
#error "BOOTLOADER_DEBUG must be 0 or 1"
#endif

#endif
