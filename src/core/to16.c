#include "to16.h"
#include "fixed.h"

void to16_run(const To16 *a)
{
	const uint16_t *pal = a->pal;
	int16_t x0 = a->x0, x1 = a->x1, y, x;
	uint8_t le = a->le, c;

	for (y = a->y0; y < a->y1; y++) {
		const uint8_t *p = a->bits + MULU16(y, a->srb);
		uint8_t *q = a->d + MULU16(y, a->drb);
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
