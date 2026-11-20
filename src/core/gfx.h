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
	uint8_t half;           /* at scale 2, render.c draws the views at scale 1 and doubles
	                         * them (slow CPUs); the platform sets it, gfx_init leaves it */
	int16_t lw, lh;         /* size in logical units, set by gfx_init */
	int16_t ox, oy;         /* where ui.c and pictures put logical 0,0, in pixels; 0 from gfx_init */
	uint8_t pa[UIC_COUNT];  /* pixel value of each UI colour on even checker squares */
	uint8_t pb[UIC_COUNT];  /* and on odd ones */
} Surface;

/* Sets up s for its depth and scale; bits, rowBytes, w and h are set by the caller.
 * Screens bigger than 160 x 160 units show the menus and dialogs in a 160
 * unit square at ox, oy (gfx_centre). */
void SEC_UI gfx_init(Surface *s);
/* Puts logical 0,0 so a 160 unit square sits in the middle (on gfx_centre(s, 1)) or at the top left. */
void SEC_UI gfx_centre(Surface *s, int on);
/* RGB of a UI colour as 0xRRGGBB, for host pictures. */
uint32_t SEC_UI gfx_ui_rgb(uint8_t c);

void SEC_UI gfx_fill(Surface *s, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c);
/* Text in the font for the surface's scale: 8x8 at scale 1, 8x16 with each
 * column doubled at scale 2, so a character is 8 logical units square. */
void SEC_UI gfx_text(Surface *s, int16_t x, int16_t y, const char *t, int16_t n, uint8_t c);
/* Copies the pixels of a rectangle from src, a surface of the same size and
 * depth; below 8 bits whole bytes go, so a few pixels either side come too. */
void SEC_UI gfx_copy(Surface *d, const Surface *src, int16_t x, int16_t y, int16_t w, int16_t h);
/* Swaps the pixels of rows y0 to y1 (not y1) between a and b, surfaces of
 * the same size and depth, where a 4x4 ordered dither value (0 to 15) is
 * from lo up to (not) hi. Steps of this fade one picture into another
 * with no third buffer: both are whole again after the last. */
void SEC_CORE gfx_swap_mask(Surface *a, Surface *b, int16_t y0, int16_t y1, uint8_t lo, uint8_t hi);
/* The golf ball cursor, BALL_W x BALL_H logical units. */
void SEC_UI gfx_ball(Surface *s, int16_t x, int16_t y);
int16_t SEC_UI gfx_ball_w(void);
int16_t SEC_UI gfx_ball_h(void);

/* Picture bands, made by tools/mkart.py: big-endian u16 first row, u16 rows,
 * u16 bytes a row, u8 depth, u8 0, then the rows packed with PackBits.
 * Put at ox, oy. 2-bit bands also go on 1, 4 and 8-bit surfaces, doubled
 * at scale 2 as they're 160 wide, and 8-bit 320 wide ones on 160 wide 8-bit surfaces at half size. Returns 0, or -1
 * if the band doesn't fit the surface or is cut short. */
int SEC_UI gfx_pic_band(Surface *s, const uint8_t *d, uint32_t len);

/* Copies a rectangle of an 8-bit surface to 16-bit colour pixels (5 red,
 * 6 green, 5 blue, high byte first, or low byte first if le) at the same
 * place in d, rb bytes a row, through the master palette. For screens left
 * at 16 bits. */
void SEC_UI gfx_to16(const Surface *s, uint8_t *d, uint16_t rb, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t le);
/* gfx_to16 for x0 to x1 and y0 to y1 (not x1 or y1), only the pixels
 * whose 4x4 ordered dither value (as gfx_swap_mask) is from lo up to (not)
 * hi: the fade's steps on 16-bit screens. */
void SEC_CORE gfx_to16_mask(const Surface *s, uint8_t *d, uint16_t rb, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t le,
                            uint8_t lo, uint8_t hi);
/* Both go through fn from now on instead of to16_run (to16.h), or back
 * to it with NULL: the platform's faster conversion, given the same job. */
struct To16Job;
void SEC_CORE gfx_to16_use(void (*fn)(const struct To16Job *a));
/* The master palette as 16-bit colour, 256 entries in the CPU's byte order. */
const uint16_t * SEC_CORE gfx_pal565(void);
/* Copies from s to d (the same size and depth) the pixels of x0 to x1 and
 * y0 to y1 whose dither value is from lo up to (not) hi, rounded out to 4
 * byte groups across: the fade's steps below 16 bits. */
void SEC_CORE gfx_copy_mask(Surface *d, const Surface *s, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t lo, uint8_t hi);

#endif
