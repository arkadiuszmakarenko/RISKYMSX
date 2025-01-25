#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 4096  // Ensure this is a power of 2 for bitwise wrapping

typedef struct {
    uint32_t *buffer;
    volatile uint8_t head;
    volatile uint8_t tail;
} CircularBuffer;

void initBuffer (CircularBuffer *cb);
int append (CircularBuffer *cb, uint32_t item);
int pop (CircularBuffer *cb, uint32_t *item);

#endif  // CIRCULAR_BUFFER_H
