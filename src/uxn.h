/*
Copyright (c) 2021 Devine Lu Linvega

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

#include <inttypes.h>

/* clang-format off */

#define PEEK2(d) (*(d) << 8 | (d)[1])
#define POKE2(d, v) { *(d) = (v) >> 8; (d)[1] = (v); }

/* clang-format on */

#define STEP_MAX 0x80000000
#define PAGE_PROGRAM 0x0100
#define PAGE_SIZE 0x10000

typedef uint8_t Uint8;
typedef int8_t Sint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint32_t Uint32;

typedef struct {
	Uint8* dat;
	Uint8* ptr;
} Stack;

typedef struct Uxn {
	Uint8 *ram;
	Uint8 *dev;
	Stack wst, rst;
	Uint16 pc;
} Uxn;

extern Uint8 emu_dei(Uxn* uxn, Uint8 addr);
extern void emu_deo(Uxn* uxn, Uint8 addr, Uint8 value);

void uxn_eval(Uxn* uxn);
