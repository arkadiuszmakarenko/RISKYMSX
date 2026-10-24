#ifndef __ROMDETECT_H
#define __ROMDETECT_H

#include <stdint.h>
#include "cart.h"

// Guess the cart mapper type of a ROM image by content analysis, using the
// same bank-switch hit heuristic as openMSX's RomFactory::guessRomType()
// mapped to the RISKYMSX CartType values.
CartType DetectCartType (const uint8_t *data, uint32_t size);

// Same, but reads the ROM image from a file on the mounted USB drive.
// Returns 1 on success, 0 when the file could not be read.
int DetectCartTypeFromFile (const char *fullPath, CartType *type);

#endif  // __ROMDETECT_H
