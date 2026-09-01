// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins

#include "tables.h"

/* Pre-computed sin/cos tables in 8.8 fixed point.
 * Generated from standard math, 256 entries = 360 degrees.
 * sin_table[i] = sin(i * 2*PI / 256) * 256
 */

WORD sin_table[TABLE_SIZE];
WORD cos_table[TABLE_SIZE];

/* Standard sine values for first quadrant (0..64) in 8.8 fixed point.
 * sine_q1[i] = round(256 * sin(i * PI / 128))
 * Index 0 -> 0, index 64 -> 256 (== sin(pi/2) * 256). */
static const WORD sine_q1[65] = {
      0,   6,  13,  19,  25,  31,  38,  44,
     50,  56,  62,  68,  74,  80,  86,  92,
     98, 104, 109, 115, 121, 126, 132, 137,
    142, 147, 152, 157, 162, 167, 172, 177,
    181, 185, 190, 194, 198, 202, 206, 209,
    213, 216, 220, 223, 226, 229, 231, 234,
    237, 239, 241, 243, 245, 247, 248, 250,
    251, 252, 253, 254, 255, 255, 256, 256,
    256
};

void tables_init(void)
{
    WORD i;

    /* Build full sine table from first quadrant */
    for (i = 0; i <= 64; i++) {
        sin_table[i] = sine_q1[i];
        sin_table[128 - i] = sine_q1[i];
        sin_table[128 + i] = -sine_q1[i];
        if (i > 0)
            sin_table[256 - i] = -sine_q1[i];
    }

    /* Cosine = sine shifted by 90 degrees (64 entries) */
    for (i = 0; i < TABLE_SIZE; i++) {
        cos_table[i] = sin_table[(i + 64) & (TABLE_SIZE - 1)];
    }
}

UWORD isqrt(UWORD val)
{
    UWORD result = 0;
    UWORD bit = 1 << 14; /* Start with highest power of 4 <= 65535 */

    while (bit > val)
        bit >>= 2;

    while (bit != 0) {
        if (val >= result + bit) {
            val -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}
