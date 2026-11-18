#include "gfx.h"
#include "fonttab.h"
#include "fixed.h"

static const uint8_t ui_grey[UIC_COUNT] = {
#define X(name, r, g, b, grey) grey,
	UI_COLOURS
#undef X
};

static const uint8_t ui_rgb[UIC_COUNT * 3] = {
#define X(name, r, g, b, grey) r, g, b,
	UI_COLOURS
#undef X
};

/* Ball sprite slots to UI colours, slot 0 is clear. */
static const uint8_t ball_col[6] = { 0, UIC_WHITE, UIC_BALL_LT, UIC_BALL_DK, UIC_TEE, UIC_TEE_DK };

void gfx_init(Surface *s)
{
	uint8_t i, top = (uint8_t)((1 << s->depth) - 1);

	s->lg = (uint8_t)(s->depth == 8 ? 3 : s->depth == 4 ? 2 : s->depth == 2 ? 1 : 0);

	for (i = 0; i < UIC_COUNT; i++) {
		if (s->depth >= 8) {
			s->pa[i] = s->pb[i] = (uint8_t)(i + 1);
		} else {
			/* Shade 0-6 in half steps of the depth's greys, white is 0. */
			uint16_t t = (uint16_t)ui_grey[i] * top * 2 / 6;
			uint8_t lo = (uint8_t)(t >> 1), hi = (uint8_t)(lo + (t & 1));
			s->pa[i] = (uint8_t)(top - lo);
			s->pb[i] = (uint8_t)(top - hi);
		}
	}
}

uint32_t gfx_ui_rgb(uint8_t c)
{
	const uint8_t *p = ui_rgb + c * 3;
	return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static inline SEC_UI uint8_t *rowp(const Surface *s, int16_t y)
{
	return s->bits + MULU16(y, s->rowBytes);
}

/* Pixel x of row p. Shifts only: the 68000 has no fast 32-bit multiply. */
static inline SEC_UI void put(const Surface *s, uint8_t *p, int16_t x, uint8_t v)
{
	uint16_t bit;
	uint8_t d, sh, m;

	if (s->lg == 3) {
		p[x] = v;
		return;
	}
	bit = (uint16_t)x << s->lg;
	d = (uint8_t)(1 << s->lg);
	sh = (uint8_t)(8 - d - (bit & 7));   /* the first pixel sits in the top bits */
	m = (uint8_t)(((1 << d) - 1) << sh);
	p += bit >> 3;
	*p = (uint8_t)((*p & ~m) | ((v << sh) & m));
}

void gfx_fill(Surface *s, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c)
{
	int16_t x1 = x + w, y1 = y + h, i, j;
	uint8_t ppb = (uint8_t)(8 >> s->lg), d = (uint8_t)(1 << s->lg);

	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x1 > s->w) x1 = s->w;
	if (y1 > s->h) y1 = s->h;
	if (x >= x1) return;
	for (j = y; j < y1; j++) {
		uint8_t *p = rowp(s, j);
		uint8_t a = (j & 1) ? s->pb[c] : s->pa[c], b = (j & 1) ? s->pa[c] : s->pb[c], pat = 0, k;
		i = x;
		if (s->lg == 3) {
			for (; i < x1; i++) p[i] = (i & 1) ? b : a;
			continue;
		}
		/* Odd pixels up to a byte edge, whole bytes, then the rest. */
		for (k = 0; k < ppb; k++) pat = (uint8_t)((pat << d) | ((k & 1) ? b : a));
		for (; i < x1 && (i & (ppb - 1)); i++) put(s, p, i, (i & 1) ? b : a);
		for (; i + ppb <= x1; i += ppb) p[(uint16_t)i >> (3 - s->lg)] = pat;
		for (; i < x1; i++) put(s, p, i, (i & 1) ? b : a);
	}
}

void gfx_text(Surface *s, int16_t x, int16_t y, const char *t, int16_t n, uint8_t c)
{
	int16_t k, r, b, px, py;
	uint8_t big = s->scale > 1, rows = big ? 16 : 8;

	for (k = 0; k < n && t[k]; k++, x += 8 * s->scale) {
		uint8_t ch = (uint8_t)t[k];
		const uint8_t *g;
		if (ch < FONT_FIRST || ch >= FONT_FIRST + FONT_COUNT) ch = '?';
		g = big ? font16 + (ch - FONT_FIRST) * 16 : font8 + (ch - FONT_FIRST) * 8;
		for (r = 0; r < rows; r++) {
			uint8_t row = g[r], *p;
			py = y + r;
			if (!row || py < 0 || py >= s->h) continue;
			p = rowp(s, py);
			for (b = 0; b < 8; b++) {
				if (!(row & (0x80 >> b))) continue;
				px = big ? x + 2 * b : x + b;
				if (px >= 0 && px < s->w) put(s, p, px, ((px ^ py) & 1) ? s->pb[c] : s->pa[c]);
				if (big && ++px >= 0 && px < s->w) put(s, p, px, ((px ^ py) & 1) ? s->pb[c] : s->pa[c]);
			}
		}
	}
}

void gfx_ball(Surface *s, int16_t x, int16_t y)
{
	int16_t i, j;

	for (j = 0; j < BALL_H; j++)
		for (i = 0; i < BALL_W; i++) {
			uint8_t v = ball_spr[j * BALL_W + i];
			if (v) gfx_fill(s, x + i * s->scale, y + j * s->scale, s->scale, s->scale, ball_col[v]);
		}
}

int16_t gfx_ball_w(void) { return BALL_W; }
int16_t gfx_ball_h(void) { return BALL_H; }

int gfx_pic_band(Surface *s, const uint8_t *d, uint32_t len)
{
	uint16_t y0, rows, rb, col = 0, row = 0;
	uint32_t i = 8;
	uint8_t *p;

	if (len < 8) return -1;
	y0 = (uint16_t)((d[0] << 8) | d[1]);
	rows = (uint16_t)((d[2] << 8) | d[3]);
	rb = (uint16_t)((d[4] << 8) | d[5]);
	if (d[6] != s->depth || rb > s->rowBytes || y0 + rows > s->h) return -1;
	p = rowp(s, (int16_t)y0);
	while (row < rows) {
		int8_t n;
		uint16_t cnt, run;
		const uint8_t *src;
		uint8_t *q;
		if (i >= len) return -1;
		n = (int8_t)d[i++];
		if (n == -128) continue;
		cnt = (uint16_t)(n >= 0 ? n + 1 : 1 - n);
		if (i + (n >= 0 ? cnt : 1) > len) return -1;
		src = d + i;
		i += n >= 0 ? cnt : 1;
		/* Copy or repeat in pieces that stop at the row's end. */
		while (cnt && row < rows) {
			run = rb - col;
			if (run > cnt) run = cnt;
			cnt -= run;
			q = p + col;
			col += run;
			if (n >= 0) {
				while (run--) *q++ = *src++;
			} else {
				uint8_t v = *src;
				while (run--) *q++ = v;
			}
			if (col == rb) {
				col = 0;
				p += s->rowBytes;
				row++;
			}
		}
	}
	return 0;
}
