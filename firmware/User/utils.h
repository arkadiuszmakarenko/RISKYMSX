#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 64  // Ensure this is a power of 2 for bitwise wrapping
#define BUFFER_MINI_SIZE 16

typedef struct {
    uint32_t *buffer;
    volatile uint32_t head;
    volatile uint32_t tail;
} CircularBuffer;

void initBuffer (CircularBuffer *cb);
void initMiniBuffer (CircularBuffer *cb);
void deinitBuffer (CircularBuffer *cb);
int append (CircularBuffer *cb, uint32_t item);
int pop (CircularBuffer *cb, uint32_t *item);
int popmini (CircularBuffer *cb, uint32_t *item);
void appendString (CircularBuffer *cb, const char *inputString);
int isFull (CircularBuffer *cb);

int strToInt (const char *str);
void intToString (int num, char *str);
int isPrintableCharacter (int value);

void handle_path (char *str);
#endif  // CIRCULAR_BUFFER_H
