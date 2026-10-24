#include "utils.h"
#include "ff.h"
#include "MSXTerminal.h"

#pragma GCC push_options
#pragma GCC optimize("Os")

extern CircularBuffer scb;

void initBuffer (CircularBuffer *cb) {
    cb->head = 0;
    cb->tail = 0;
}

void initMiniBuffer (CircularBuffer *cb) {
    cb->head = 0;
    cb->tail = 0;
}

void deinitBuffer (CircularBuffer *cb) {
    cb->head = 0;
    cb->tail = 0;
}

int isFull (CircularBuffer *cb) {
    uint32_t next = (cb->head + 1) & (BUFFER_SIZE - 1);  // Compute the next position using bitwise AND
    if (next == cb->tail) {
        return 1;
    }
    return 0;
}

void flushBuffer (CircularBuffer *cb) {
    cb->head = 0;
    cb->tail = 0;
}

int append (CircularBuffer *cb, uint32_t item) {
    uint32_t next = (cb->head + 1) & (BUFFER_SIZE - 1);  // Compute the next position using bitwise AND
    while (next == cb->tail) { };
    cb->buffer[cb->head] = item;
    cb->head = next;
    return 0;  // Success
}

int pop (CircularBuffer *cb, uint32_t *item) {
    if (cb->head == cb->tail) {
        return -1;  // Buffer is empty
    }
    *item = cb->buffer[cb->tail];
    cb->tail = (cb->tail + 1) & (BUFFER_SIZE - 1);  // Update tail using bitwise AND
    return 0;                                       // Success
}

int popmini (CircularBuffer *cb, uint32_t *item) {
    if (cb->head == cb->tail) {
        return -1;  // Buffer is empty
    }
    *item = cb->buffer[cb->tail];
    cb->tail = (cb->tail + 1) & (BUFFER_MINI_SIZE - 1);  // Update tail using bitwise AND
    return 0;                                            // Success
}

void appendStringUpToLen (CircularBuffer *cb, const char *inputString, int len) {
    int slen = strlen(inputString);
    len = (slen < len)?slen:len;
    for (int i = 0; i < len; i++) {
        append (cb, inputString[i]);
    }
}

void appendString (CircularBuffer *cb, const char *inputString) {
    appendStringUpToLen(cb, inputString, strlen (inputString));
}

int intToString (int num, char *str) {
    int i = 0;
    int isNegative = 0;

    // Handle negative numbers
    if (num < 0) {
        isNegative = 1;
        num = -num;
    }

    // Process each digit of the number
    do {
        str[i++] = (num % 10) + '0';
        num /= 10;
    } while (num > 0);

    // Add negative sign if the number is negative
    if (isNegative) {
        str[i++] = '-';
    }

    // Null-terminate the string
    str[i] = '\0';

    // Reverse the string
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
    return i;
}

int strToInt (const char *str) {
    int result = 0;
    int sign = 1;
    int i = 0;

    // Handle optional leading whitespace
    while (str[i] == ' ') {
        i++;
    }

    // Handle optional sign
    if (str[i] == '-') {
        sign = -1;
        i++;
    } else if (str[i] == '+') {
        i++;
    }

    // Convert characters to integer
    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
        i++;
    }

    // Apply sign
    result *= sign;

    return result;
}

// ---------------------------------------------------------------------------
// Alphabetical file cache
// ---------------------------------------------------------------------------

#define SC_MAX_ENTRIES 224 // maximum entries cached (and sorted) per folder
#define SC_POOL_SIZE 6144  // bytes available for the file names themselves

// Cache for the listing of the folder currently shown. FatFs returns entries
// in on-disk (FAT) order, so the whole folder is read into this cache once and
// sorted alphabetically before paging through it. Only a bounded number of
// entries fits in RAM: folders with more files show the first SC_MAX_ENTRIES
// alphabetically and the remaining ones in USB order after those.
static struct {
    uint8_t valid;                    // cache matches the requested folder
    char folder[256];                 // folder this cache was built for
    uint16_t count;                   // number of sorted entries below
    uint16_t rawCount;                // raw directory entries consumed so far
    uint16_t nameOff[SC_MAX_ENTRIES]; // offset of each name in the pool
    uint8_t isDir[SC_MAX_ENTRIES];
    uint32_t size[SC_MAX_ENTRIES];    // in bytes, as reported by FatFs
    uint16_t poolUsed;
    char pool[SC_POOL_SIZE];
} sc;

// Call after (re)mounting the USB stick: the cached listing may be stale.
void InvalidateFileCache (void) {
    sc.valid = 0;
}

// Case-insensitive comparison of two cached entries, directories first.
static int scCompare (uint16_t offA, uint8_t isDirA, uint16_t offB, uint8_t isDirB) {
    if (isDirA != isDirB) {
        return isDirA ? -1 : 1;
    }
    const unsigned char *na = (const unsigned char *)sc.pool + offA;
    const unsigned char *nb = (const unsigned char *)sc.pool + offB;
    for (;;) {
        unsigned char ca = *na++;
        unsigned char cb = *nb++;
        if (ca >= 'A' && ca <= 'Z') ca += 0x20;
        if (cb >= 'A' && cb <= 'Z') cb += 0x20;
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0;
    }
}

// Insertion sort over the parallel cache arrays (counts are small).
static void scSort (void) {
    for (int i = 1; i < (int)sc.count; i++) {
        uint16_t off = sc.nameOff[i];
        uint8_t isDir = sc.isDir[i];
        uint32_t size = sc.size[i];
        int j = i - 1;
        while (j >= 0 && scCompare (off, isDir, sc.nameOff[j], sc.isDir[j]) < 0) {
            sc.nameOff[j + 1] = sc.nameOff[j];
            sc.isDir[j + 1] = sc.isDir[j];
            sc.size[j + 1] = sc.size[j];
            j--;
        }
        sc.nameOff[j + 1] = off;
        sc.isDir[j + 1] = isDir;
        sc.size[j + 1] = size;
    }
}

int listFiles (uint8_t folder[256], FileEntry FileArray[], int FileArraySize, int page) {
    DIR dir;
    FILINFO fno;
    int firstItem = (page - 1) * FileArraySize;
    int size = 0;

    if (page < 1) {
        page = 1;
        firstItem = 0;
    }

    // Rebuild the cache when a different folder is opened or after a remount
    if (!sc.valid || strcmp (sc.folder, (const char *)folder) != 0) {
        sc.valid = 1;
        strncpy (sc.folder, (const char *)folder, sizeof (sc.folder) - 1);
        sc.folder[sizeof (sc.folder) - 1] = 0;
        sc.count = 0;
        sc.poolUsed = 0;

        if (f_opendir (&dir, sc.folder) == FR_OK) {
            while (sc.count < SC_MAX_ENTRIES) {
                int len;
                if (f_readdir (&dir, &fno) != FR_OK || fno.fname[0] == 0) break;
                len = strlen (fno.fname);
                if ((uint32_t)sc.poolUsed + len + 1 > sizeof (sc.pool)) break; // pool full
                memcpy (sc.pool + sc.poolUsed, fno.fname, len + 1);
                sc.nameOff[sc.count] = sc.poolUsed;
                sc.isDir[sc.count] = (fno.fattrib & AM_DIR) ? 1 : 0;
                sc.size[sc.count] = (uint32_t)fno.fsize;
                sc.poolUsed += len + 1;
                sc.count++;
            }
            f_closedir (&dir);
        }
        sc.rawCount = sc.count;
        scSort();
    }

    // Page inside the alphabetically sorted cache
    if (firstItem < (int)sc.count) {
        int n = (int)sc.count - firstItem;
        if (n > FileArraySize) n = FileArraySize;
        for (int i = 0; i < n; i++) {
            int e = firstItem + i;
            strncpy (FileArray[i].name, sc.pool + sc.nameOff[e], sizeof (FileArray[i].name) - 1);
            FileArray[i].name[sizeof (FileArray[i].name) - 1] = 0;
            FileArray[i].isDir = sc.isDir[e];
            FileArray[i].size_kb = sc.size[e];
        }
        return n;
    }

    // Page beyond the cache: continue in USB order, skipping cached entries
    if (f_opendir (&dir, (char *)folder) != FR_OK) {
        return 0;
    }
    for (int idx = 0; idx < (int)sc.rawCount; idx++) {
        if (f_readdir (&dir, &fno) != FR_OK || fno.fname[0] == 0) {
            f_closedir (&dir);
            return size;
        }
    }
    while (size < FileArraySize) {
        if (f_readdir (&dir, &fno) != FR_OK || fno.fname[0] == 0) break;
        strncpy (FileArray[size].name, fno.fname, sizeof (FileArray[size].name) - 1);
        FileArray[size].name[sizeof (FileArray[size].name) - 1] = 0;
        if (fno.fattrib & AM_DIR) {
            FileArray[size].isDir = 1;
            FileArray[size].size_kb = 0;
        } else {
            FileArray[size].isDir = 0;
            FileArray[size].size_kb = (uint32_t)fno.fsize;
        }
        size++;
    }
    f_closedir (&dir);
    return size;
}

void CombinePath (char *dest, const char *folder, const char *filename) {
    strcpy (dest, folder);
    // Add slash if needed (but not for root "/")
    if (folder[0] != '\0' && folder[strlen (folder) - 1] != '/' && strcmp (folder, "/") != 0) {
        strcat (dest, "/");
    }
    strcat (dest, filename);
}

// Waits until the buffer is empty (no data to be sent)
void waitBufferEmpty(CircularBuffer *cb) {
    while (cb->head != cb->tail) {
        // Optionally, add a small delay here if needed to avoid busy-waiting
    }
}

#pragma GCC pop_options