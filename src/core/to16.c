#include "to16.h"
#include "fixed.h"
#include "pal.h"

#ifndef __m68k__
/* Mix the first neighbouring terrain material into the edge pixel. */
static uint16_t SEC_CORE terrain_edge(uint16_t base, uint8_t ramp, const uint16_t *pal,
                                    uint8_t left, uint8_t right, uint8_t up, uint8_t down)
{
	uint8_t neighbour;
	uint16_t other;
	uint32_t a, b, mix;
	const uint32_t channels = 0x07E0F81FUL;
	const uint8_t shade = PAL_RAMP_N - 1;

	if (left < PAL_TERRAIN_N && ((left ^ ramp) & ~shade)) neighbour = left;
	else if (right < PAL_TERRAIN_N && ((right ^ ramp) & ~shade)) neighbour = right;
	else if (up < PAL_TERRAIN_N && ((up ^ ramp) & ~shade)) neighbour = up;
	else if (down < PAL_TERRAIN_N && ((down ^ ramp) & ~shade)) neighbour = down;
	else return base;
	other = pal[PAL_TERRAIN + neighbour];
	/* Mix three eighths of the neighbour, rounding each RGB565 component. */
	a = ((uint32_t)base << 16 | base) & channels;
	b = ((uint32_t)other << 16 | other) & channels;
	mix = ((5 * a + 3 * b + 0x00802004UL) >> 3) & channels;
	return (uint16_t)(mix | (mix >> 16));
}

static inline uint16_t terrain_colour(const To16 *a, const uint8_t *p,
                                      const uint8_t *above, const uint8_t *below, int16_t x)
{
	uint8_t c = p[x], ramp = (uint8_t)(c - PAL_TERRAIN);
	uint16_t v = a->pal[c];

	if (ramp < PAL_TERRAIN_N && x > 0 && x + 1 < a->width) {
		uint8_t left = (uint8_t)(p[x - 1] - PAL_TERRAIN);
		uint8_t right = (uint8_t)(p[x + 1] - PAL_TERRAIN);
		uint8_t up = (uint8_t)(above[x] - PAL_TERRAIN);
		uint8_t down = (uint8_t)(below[x] - PAL_TERRAIN);
		if (((left ^ ramp) | (right ^ ramp) | (up ^ ramp) | (down ^ ramp)) & ~(PAL_RAMP_N - 1))
			v = terrain_edge(v, ramp, a->pal, left, right, up, down);
	}
	return v;
}

#if defined(__arm__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
typedef uint32_t To16Word __attribute__((may_alias));
typedef uint16_t To16Pixel __attribute__((may_alias));

/* Terrain ramps start four short of an eight-colour boundary. Add four
 * independently and keep the ramp bits. Terrain colours never use bit 7. */
static inline uint32_t terrain_groups(uint32_t word)
{
	return ((word & 0x7f7f7f7fUL) + 0x04040404UL) & 0x78787878UL;
}

static void terrain_row_le(const To16 *a, const uint8_t *p, To16Pixel *q)
{
	const uint8_t *above = p - a->srb, *below = p + a->srb;
	const uint16_t *pal = a->pal;
	int16_t x = a->x0;
	int16_t stop = a->x1 < a->width ? a->x1 : (int16_t)(a->width - 1);
	int words = !((uintptr_t)q & 3);

	if (!x && x < a->x1) {
		q[x] = pal[p[x]];
		x++;
	}
	for (; x < stop && (x & 3); x++)
		q[x] = terrain_colour(a, p, above, below, x);
	for (; x + 4 <= stop; x += 4) {
		uint32_t current = terrain_groups(*(const To16Word *)(p + x));
		uint32_t up = terrain_groups(*(const To16Word *)(above + x));
		uint32_t down = terrain_groups(*(const To16Word *)(below + x));
		uint32_t neighbours = (current << 8) | (uint32_t)(((uint16_t)p[x - 1] + 4) & 0x78);

		/* Screen runs in one ramp need only palette lookups. */
		if (current == up && current == down && current == neighbours &&
		    (current >> 24) == (uint32_t)(((uint16_t)p[x + 4] + 4) & 0x78)) {
			uint16_t a0 = pal[p[x]], a1 = pal[p[x + 1]];
			uint16_t b0 = pal[p[x + 2]], b1 = pal[p[x + 3]];
			if (words) {
				To16Word *out = (To16Word *)(q + x);
				out[0] = a0 | ((uint32_t)a1 << 16);
				out[1] = b0 | ((uint32_t)b1 << 16);
			} else {
				q[x] = a0;
				q[x + 1] = a1;
				q[x + 2] = b0;
				q[x + 3] = b1;
			}
		} else {
			int16_t i;
			for (i = x; i < x + 4; i++) q[i] = terrain_colour(a, p, above, below, i);
		}
	}
	for (; x < a->x1; x++) q[x] = terrain_colour(a, p, above, below, x);
}
#endif

static void terrain_row(const To16 *a, const uint8_t *p, uint8_t *q, int16_t y)
{
	const uint8_t *above = p - a->srb, *below = p + a->srb;
	int16_t x;

	if (a->mask) {
		uint8_t column;
		const uint8_t *pattern = a->dith + ((y & 3) << 2);
		for (column = 0; column < 4; column++) {
			if (pattern[column] < a->lo || pattern[column] >= a->hi) continue;
			for (x = (int16_t)(((a->x0 + 3 - column) & ~3) + column); x < a->x1; x += 4) {
				uint16_t v = terrain_colour(a, p, above, below, x);
				if (a->le) v = (uint16_t)((v << 8) | (v >> 8));
				q[x << 1] = (uint8_t)(v >> 8);
				q[(x << 1) + 1] = (uint8_t)v;
			}
		}
		return;
	}
	for (x = a->x0; x < a->x1; x++) {
		uint16_t v = terrain_colour(a, p, above, below, x);
		if (a->le) v = (uint16_t)((v << 8) | (v >> 8));
		q[x << 1] = (uint8_t)(v >> 8);
		q[(x << 1) + 1] = (uint8_t)v;
	}
}
#endif

void to16_run(const To16 *a)
{
	const uint16_t *pal = a->pal;
	int16_t x0 = a->x0, x1 = a->x1, y, x;
	uint8_t le = a->le, c;

	for (y = a->y0; y < a->y1; y++) {
		const uint8_t *p = a->bits + MULU16(y, a->srb);
		uint8_t *q = a->d + MULU16(y, a->drb);
#ifndef __m68k__
		if (y > 0 && y + 1 < a->smooth_y1) {
#if defined(__arm__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
			if (!a->mask && le && !(((uintptr_t)p | a->srb) & 3) && !((uintptr_t)q & 1))
				terrain_row_le(a, p, (To16Pixel *)q);
			else
#endif
			terrain_row(a, p, q, y);
			continue;
		}
#endif
		if (!a->mask) {
			p += x0;
			q += (uint16_t)x0 << 1;
			for (x = x0; x < x1; x++) {
				uint16_t v = pal[*p++];
				if (le) v = (uint16_t)((v << 8) | (v >> 8));
				*q++ = (uint8_t)(v >> 8);
				*q++ = (uint8_t)v;
			}
			continue;
		}
		/* each of the row's 4 columns of the pattern whose value is in the step */
		for (c = 0; c < 4; c++) {
			uint8_t t = a->dith[((y & 3) << 2) + c];
			if (t < a->lo || t >= a->hi) continue;
			for (x = (int16_t)(((x0 + 3 - c) & ~3) + c); x < x1; x += 4) {
				uint16_t v = pal[p[x]];
				if (le) v = (uint16_t)((v << 8) | (v >> 8));
				q[x << 1] = (uint8_t)(v >> 8);
				q[(x << 1) + 1] = (uint8_t)v;
			}
		}
	}
}

void to16_pal_le(uint8_t *d, const uint16_t *pal)
{
	int16_t i;
	for (i = 0; i < 256; i++) {
		*d++ = (uint8_t)pal[i];
		*d++ = (uint8_t)(pal[i] >> 8);
	}
}
