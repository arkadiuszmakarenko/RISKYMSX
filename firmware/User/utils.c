#include "utils.h"
#include "ff.h"
#include "MSXTerminal.h"

#pragma GCC push_options
#pragma GCC optimize("Ofast")

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

int listFiles (uint8_t folder[256], FileEntry FileArray[], int FileArraySize, int page) {
    DIR dir;
    FILINFO fno;
    int firstItem = (page - 1) * FileArraySize;
    int size = 0;
    int idx = 0;
    FRESULT fres;

    fres = f_opendir (&dir, (char *)folder);
    if (fres != FR_OK) {
        return 0;
    }

    // Skip entries before firstItem
    while (idx < firstItem) {
        fres = f_readdir (&dir, &fno);
        if (fres != FR_OK || fno.fname[0] == 0) {
            f_closedir (&dir);
            return size;
        }
        idx++;
    }

    // Collect up to FileArraySize entries for this page
    while (size < FileArraySize) {
        fres = f_readdir (&dir, &fno);
        if (fres != FR_OK || fno.fname[0] == 0) {
            break;
        }
        strncpy ((char *)FileArray[size].name, fno.fname, 255);
        if (fno.fattrib & AM_DIR) {
            FileArray[size].isDir = 1;
            FileArray[size].size_kb = 0;
        } else {
            FileArray[size].isDir = 0;
            FileArray[size].size_kb = fno.fsize;
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