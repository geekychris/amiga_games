// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* Host build of game.cpp and levels.cpp for the route tools: the few
 * <exec/types.h> types they use, at the Amiga's sizes. */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
#include <stdint.h>
typedef int32_t  LONG;
typedef uint32_t ULONG;
typedef int16_t  WORD;
typedef uint16_t UWORD;
typedef int8_t   BYTE;
typedef uint8_t  UBYTE;
typedef int16_t  BOOL;
#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif
#endif
