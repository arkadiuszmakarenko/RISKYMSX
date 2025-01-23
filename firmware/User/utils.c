#include "utils.h"
#pragma GCC push_options
#pragma GCC optimize("Ofast")

void initBuffer (CircularBuffer *cb) {
    cb->buffer = (uint32_t *)malloc (BUFFER_SIZE * sizeof (uint32_t));
    cb->head = 0;
    cb->tail = 0;
}

int append (CircularBuffer *cb, uint32_t item) {
    uint8_t next = (cb->head + 1) & (BUFFER_SIZE - 1);  // Compute the next position using bitwise AND
    if (next == cb->tail) {
        return -1;                                      // Buffer is full
    }
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

#pragma GCC pop_options