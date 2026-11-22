/* 8-bit palette indexes to 16-bit colour, the loop behind gfx_to16 and
 * gfx_to16_mask. The same C is built for the 68k (core segment), the host
 * and as ARM code for OS 5 (src/arm/to16arm.c), so it uses no tables or
 * globals of its own: the palette and dither table come in the job. */
#ifndef TO16_H
#define TO16_H

#include <stdint.h>
#include "section.h"

/* On the 68k this struct is also the ARM code's argument block, read there
 * big endian at these byte offsets (TO16_*), so keep the layout. */
typedef struct To16Job {
	const uint8_t *bits;    /* 0: the 8-bit surface */
	uint8_t *d;             /* 4: the 16-bit pixels */
	const uint16_t *pal;    /* 8: 16-bit colour of each index, in the CPU's own byte order */
	const uint8_t *dith;    /* 12: the 4x4 ordered dither values, row by row */
	uint16_t srb, drb;      /* 16, 18: bytes a row of bits and of d */
	int16_t x0, y0, x1, y1; /* 20 to 26: the rectangle, x1 and y1 not in it, already clipped */
	uint8_t le;             /* 28: low byte first in d */
	uint8_t mask;           /* 29: only the pixels whose dither value is from lo up to (not) hi */
	uint8_t lo, hi;         /* 30, 31 */
	int16_t width;          /* 32: source columns */
	int16_t smooth_y1;      /* 34: terrain rows, 0 for no smoothing */
} To16;

#define TO16_BITS 0
#define TO16_D    4
#define TO16_PAL  8
#define TO16_DITH 12
#define TO16_SRB  16
#define TO16_DRB  18
#define TO16_X0   20
#define TO16_Y0   22
#define TO16_X1   24
#define TO16_Y1   26
#define TO16_LE   28
#define TO16_MASK 29
#define TO16_LO   30
#define TO16_HI   31
#define TO16_WIDTH 32
#define TO16_SMOOTH_Y1 34
#define TO16_SIZE 36

void SEC_CORE to16_run(const To16 *a);
/* Writes pal (256 colours) to d as 512 bytes low byte first: the table
 * the ARM code reads as its own byte order. */
void SEC_CORE to16_pal_le(uint8_t *d, const uint16_t *pal);

#endif
