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
// Alphabetical page selection
// ---------------------------------------------------------------------------

#define SB_MAX_PAGES 40    // page boundaries remembered per folder
#define SB_POOL_SIZE 1024  // bytes reserved for the boundary names

// Listing files alphabetically requires looking at the whole folder, but RAM
// is too small to hold an entire folder listing. Instead each page is selected
// straight from the USB stick: a page is the smallest FileArraySize entries
// that come strictly after the last entry of the previous page. Only those
// last names (boundaries) of already visited pages are remembered here, which
// is enough to move to the next page and back, and to re-walk to any later
// page when a boundary is not remembered. Folders of any size are listed in
// full alphabetical order.
static struct {
    uint8_t valid;              // boundary info matches the requested folder
    char folder[256];           // folder the boundaries were collected for
    uint16_t count;             // number of remembered boundaries
    uint16_t off[SB_MAX_PAGES]; // pool offset of the boundary of page (index+1)
    uint16_t poolUsed;
    char pool[SB_POOL_SIZE];    // per boundary: isDir byte + zero terminated name
} sb;

// Call after (re)mounting the USB stick: the remembered boundaries may be stale.
void InvalidateFileCache (void) {
    sb.valid = 0;
}

// Case-insensitive comparison of two entries, directories first.
static int sbCompare (uint8_t isDirA, const char *na, uint8_t isDirB, const char *nb) {
    if (isDirA != isDirB) {
        return isDirA ? -1 : 1;
    }
    for (;;) {
        unsigned char ca = (unsigned char)*na++;
        unsigned char cb = (unsigned char)*nb++;
        if (ca >= 'A' && ca <= 'Z') ca += 0x20;
        if (cb >= 'A' && cb <= 'Z') cb += 0x20;
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0;
    }
}

// Remember the last entry of a full page as the boundary of that page.
// Boundaries are stored contiguously from page 1 on: once the pool is full,
// later pages simply are not remembered (they get walked again instead), so
// every slot below sb.count always points to a valid boundary.
static void sbStoreBoundary (int page, const FileEntry *last) {
    size_t len;
    int slot = page - 1;
    if (slot >= SB_MAX_PAGES || slot != (int)sb.count) {
        return; // beyond the remembered range or already stored this session
    }
    len = strlen (last->name) + 1;
    if ((uint32_t)sb.poolUsed + 1 + len > sizeof (sb.pool)) {
        return; // pool full: further pages stay reachable, they just re-walk
    }
    sb.off[slot] = sb.poolUsed;
    sb.pool[sb.poolUsed++] = (char)last->isDir;
    memcpy (sb.pool + sb.poolUsed, last->name, len);
    sb.poolUsed += len;
    sb.count = slot + 1;
}

// Fill FileArray with the smallest FileArraySize entries that come strictly
// after the boundary entry (boundName == NULL = from the very beginning of
// the folder) and return how many entries were collected.
static int sbSelectPage (const char *folder, uint8_t boundIsDir, const char *boundName, FileEntry FileArray[], int FileArraySize) {
    DIR dir;
    FILINFO fno;
    FileEntry e;
    int size = 0;

    if (f_opendir (&dir, folder) != FR_OK) {
        return 0;
    }
    while (f_readdir (&dir, &fno) == FR_OK && fno.fname[0] != 0) {
        e.isDir = (fno.fattrib & AM_DIR) ? 1 : 0;
        e.size_kb = e.isDir ? 0 : (uint32_t)fno.fsize;
        strncpy (e.name, fno.fname, sizeof (e.name) - 1);
        e.name[sizeof (e.name) - 1] = 0;

        // skip everything up to and including the boundary
        if (boundName != NULL && sbCompare (e.isDir, e.name, boundIsDir, boundName) <= 0) {
            continue;
        }
        // insert into the sorted page, keeping only the smallest entries
        if (size < FileArraySize || sbCompare (e.isDir, e.name, FileArray[size - 1].isDir, FileArray[size - 1].name) < 0) {
            int i = size < FileArraySize ? size++ : size - 1;
            while (i > 0 && sbCompare (e.isDir, e.name, FileArray[i - 1].isDir, FileArray[i - 1].name) < 0) {
                FileArray[i] = FileArray[i - 1];
                i--;
            }
            FileArray[i] = e;
        }
    }
    f_closedir (&dir);
    return size;
}

int listFiles (uint8_t folder[256], FileEntry FileArray[], int FileArraySize, int page) {
    const char *boundName = NULL;
    uint8_t boundIsDir = 0;
    char walkName[256]; // last entry of the previous page while walking
    int size = 0;
    int k;

    if (page < 1) {
        page = 1;
    }

    // Start over when a different folder is opened or after a remount
    if (!sb.valid || strcmp (sb.folder, (const char *)folder) != 0) {
        sb.valid = 1;
        strncpy (sb.folder, (const char *)folder, sizeof (sb.folder) - 1);
        sb.folder[sizeof (sb.folder) - 1] = 0;
        sb.count = 0;
        sb.poolUsed = 0;
    }

    if (page > 1 && page - 2 < (int)sb.count) {
        // The boundary of the previous page is remembered: select directly
        boundIsDir = (uint8_t)sb.pool[sb.off[page - 2]];
        boundName = sb.pool + sb.off[page - 2] + 1;
        k = page;
    } else if (page == 1) {
        k = 1; // select from the beginning of the folder
    } else {
        // The page has not been reached before: walk from the last remembered
        // boundary, using each visited page's last entry as the next boundary
        k = (int)sb.count + 1;
        if (k > 1) {
            boundIsDir = (uint8_t)sb.pool[sb.off[k - 2]];
            boundName = sb.pool + sb.off[k - 2] + 1;
        }
    }

    for (; k <= page; k++) {
        size = sbSelectPage (sb.folder, boundIsDir, boundName, FileArray, FileArraySize);
        if (size < FileArraySize) {
            // last page of the folder (empty when past the end)
            return (k == page) ? size : 0;
        }
        // use this page's last entry as the boundary for the next one and
        // remember it for later page turns (skipped when the pool is full,
        // in which case those pages just get walked again)
        strcpy (walkName, FileArray[size - 1].name);
        boundIsDir = FileArray[size - 1].isDir;
        boundName = walkName;
        sbStoreBoundary (k, &FileArray[size - 1]);
    }
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