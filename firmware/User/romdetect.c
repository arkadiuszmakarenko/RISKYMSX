#include "romdetect.h"

#include <string.h>

#include "ff.h"
#include "programflash.h"

#if defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("Os")
#endif

enum {
    GUESS_KONAMI = 0,
    GUESS_KONAMI_SCC,
    GUESS_ASCII8,
    GUESS_ASCII16,
    GUESS_COUNT
};

// MegaROMs switch banks by writing the bank register with LD (nn),A
// (0x32 lo hi); the target address nn identifies the mapper type. Score one
// hit per matching sequence, following the openMSX guessRomType() heuristic.
static void ScoreBankWrite (uint8_t b0, uint8_t b1, uint8_t b2, uint32_t *guess) {
    if (b0 != 0x32) {
        return;
    }
    switch ((uint16_t)(b1 | ((uint16_t)b2 << 8))) {
    case 0x5000:
    case 0x9000:
    case 0xB000:
        guess[GUESS_KONAMI_SCC]++;
        break;
    case 0x4000:
    case 0x8000:
    case 0xA000:
        guess[GUESS_KONAMI]++;
        break;
    case 0x6800:
    case 0x7800:
        guess[GUESS_ASCII8]++;
        break;
    case 0x6000:
        guess[GUESS_KONAMI]++;
        guess[GUESS_ASCII8]++;
        guess[GUESS_ASCII16]++;
        break;
    case 0x7000:
        guess[GUESS_KONAMI_SCC]++;
        guess[GUESS_ASCII8]++;
        guess[GUESS_ASCII16]++;
        break;
    case 0x77FF:
        guess[GUESS_ASCII16]++;
        break;
    default:
        break;
    }
}

static const CartType kGuessCartType[GUESS_COUNT] = {
    KonamiWithoutSCC,
    KonamiWithSCC,
    ASCII8k,
    ASCII16k,
};

// header points to the first (up to 24) bytes of the image, size is the full
// image size in bytes. header may be NULL/short when size < 24 (guards below).
static CartType DecideCartType (const uint8_t *header, uint32_t size, uint32_t *guess) {
    if (header != 0 && size >= 24) {
        if (memcmp (header + 16, "ROM_NE16", 8) == 0) {
            return NEO16;
        }
        if (memcmp (header + 16, "ROM_NEO8", 8) == 0) {
            return NEO8;
        }
    }
    if (size < 0x10000) {
        if (size <= 0x4000) {
            return ROM16k;
        }
        if (size <= 0x8000) {
            return ROM32k;
        }
        return ROM48k;
    }
    if (size == 0x10000 && header != 0 && !(header[0] == 'A' && header[1] == 'B')) {
        // 64KB ROMs without a cartridge header are plain, not memory mapped
        return ROM48k;
    }
    // ASCII8 loses ties to Konami SCC (openMSX gives it a one-hit penalty)
    if (guess[GUESS_ASCII8] > 0) {
        guess[GUESS_ASCII8]--;
    }
    int best = -1;
    for (int i = 0; i < GUESS_COUNT; i++) {
        if (guess[i] > 0 && (best < 0 || guess[i] >= guess[best])) {
            best = i;
        }
    }
    // No bank switches found: a generic 8KB mapper is the safest guess
    return kGuessCartType[best < 0 ? GUESS_ASCII8 : best];
}

CartType DetectCartType (const uint8_t *data, uint32_t size) {
    uint32_t guess[GUESS_COUNT] = {0, 0, 0, 0};
    if (data != 0 && size >= 3) {
        for (uint32_t i = 0; i + 2 < size; i++) {
            ScoreBankWrite (data[i], data[i + 1], data[i + 2], guess);
        }
    }
    return DecideCartType (data, size, guess);
}

int DetectCartTypeFromFile (const char *fullPath, CartType *type) {
    extern uint8_t IAPLoadBuffer[DEF_MAX_IAP_BUFFER_LEN];
    FIL file;
    UINT bytesRead = 0;
    uint8_t header[24];
    uint8_t headerFill = 0;
    uint8_t carry[2];
    uint8_t carryLen = 0;
    uint32_t guess[GUESS_COUNT] = {0, 0, 0, 0};

    if (f_open (&file, fullPath, FA_READ) != FR_OK) {
        return 0;
    }
    uint32_t remaining = f_size (&file);
    const uint32_t size = remaining;

    while (remaining > 0) {
        UINT want = remaining > DEF_MAX_IAP_BUFFER_LEN ? DEF_MAX_IAP_BUFFER_LEN : (UINT)remaining;
        if (f_read (&file, IAPLoadBuffer, want, &bytesRead) != FR_OK || bytesRead == 0) {
            f_close (&file);
            return 0;
        }
        // Keep the first bytes for the header/signature checks
        if (headerFill < sizeof (header)) {
            uint8_t n = (uint8_t)(sizeof (header) - headerFill);
            if (n > bytesRead) {
                n = (uint8_t)bytesRead;
            }
            memcpy (header + headerFill, IAPLoadBuffer, n);
            headerFill = (uint8_t)(headerFill + n);
        }
        // Score the two windows straddling the previous chunk boundary
        if (carryLen >= 2) {
            ScoreBankWrite (carry[0], carry[1], IAPLoadBuffer[0], guess);
        }
        if (carryLen >= 1 && bytesRead >= 2) {
            ScoreBankWrite (carry[carryLen - 1], IAPLoadBuffer[0], IAPLoadBuffer[1], guess);
        }
        for (UINT i = 0; (UINT)(i + 2) < bytesRead; i++) {
            ScoreBankWrite (IAPLoadBuffer[i], IAPLoadBuffer[i + 1], IAPLoadBuffer[i + 2], guess);
        }
        // Remember the chunk tail for the next iteration
        if (bytesRead >= 2) {
            carry[0] = IAPLoadBuffer[bytesRead - 2];
            carry[1] = IAPLoadBuffer[bytesRead - 1];
            carryLen = 2;
        } else if (carryLen >= 2) {
            carry[0] = carry[1];
            carry[1] = IAPLoadBuffer[0];
        } else if (carryLen == 1) {
            carry[1] = IAPLoadBuffer[0];
            carryLen = 2;
        } else {
            carry[0] = IAPLoadBuffer[0];
            carryLen = 1;
        }
        remaining -= bytesRead;
    }
    f_close (&file);
    *type = DecideCartType (header, size, guess);
    return 1;
}

#if defined(__GNUC__)
#pragma GCC pop_options
#endif
