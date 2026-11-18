/* Drawing into a memory surface at the screen's own depth: fills, the game's
 * bitmap fonts, the ball cursor and the packed pictures. Coordinates here are
 * pixels; ui.h works in logical 160 x 160 units. No platform headers. */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include "section.h"
#include "uicol.h"

/* Pixels are packed left to right from the top bit, 1, 2, 4 or 8 bits each.
 * Below 8 bits 0 is white and the top value black (Palm order); at 8 bits a
 * pixel is a palette index. */
typedef struct {
	uint8_t *bits;
	uint16_t rowBytes;
	int16_t w, h;
	uint8_t depth;
	uint8_t lg;             /* log2 of depth, set by gfx_init */
	uint8_t scale;          /* pixels per logical unit: 1 on 160 wide screens, 2 on 320 */
	uint8_t pa[UIC_COUNT];  /* pixel value of each UI colour on even checker squares */
	uint8_t pb[UIC_COUNT];  /* and on odd ones */
} Surface;

/* Sets up s for its depth and scale; bits, rowBytes, w and h are set by the caller. */
void SEC_UI gfx_init(Surface *s);
/* RGB of a UI colour as 0xRRGGBB, for host pictures. */
uint32_t SEC_UI gfx_ui_rgb(uint8_t c);

void SEC_UI gfx_fill(Surface *s, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c);
/* Text in the font for the surface's scale: 8x8 at scale 1, 8x16 with each
 * column doubled at scale 2, so a character is 8 logical units square. */
void SEC_UI gfx_text(Surface *s, int16_t x, int16_t y, const char *t, int16_t n, uint8_t c);
/* Copies the pixels of a rectangle from src, a surface of the same size and
 * depth; below 8 bits whole bytes go, so a few pixels either side come too. */
void SEC_UI gfx_copy(Surface *d, const Surface *src, int16_t x, int16_t y, int16_t w, int16_t h);
/* The golf ball cursor, BALL_W x BALL_H logical units. */
void SEC_UI gfx_ball(Surface *s, int16_t x, int16_t y);
int16_t SEC_UI gfx_ball_w(void);
int16_t SEC_UI gfx_ball_h(void);

/* Picture bands, made by tools/mkart.py: big-endian u16 first row, u16 rows,
 * u16 bytes a row, u8 depth, u8 0, then the rows packed with PackBits.
 * Returns 0, or -1 if the band doesn't fit the surface or is cut short. */
int SEC_UI gfx_pic_band(Surface *s, const uint8_t *d, uint32_t len);

#endif
