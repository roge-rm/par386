#include "gfx.h"
#include "fonttab.h"
#include "fixed.h"
#include "pal565.h"
#include "pal.h"
#include "to16.h"

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
	s->lw = (int16_t)(s->w / s->scale);
	s->lh = (int16_t)(s->h / s->scale);
	s->ox = s->oy = 0;

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

void gfx_centre(Surface *s, int on)
{
	/* whole bytes across at every depth: a multiple of 8 pixels */
	s->ox = on ? (int16_t)(((s->lw - 160) / 2 * s->scale) & ~7) : 0;
	s->oy = on ? (int16_t)((s->lh - 160) / 2 * s->scale) : 0;
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

/* Each bit of a nibble as two bits, for 2-bit glyph rows. */
static const uint8_t exp4[16] = {
	0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F, 0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF
};

/* One 8x8 glyph on a 2-bit surface a byte at a time; it must lie inside the surface across. */
static void SEC_UI text2(Surface *s, int16_t x, int16_t y, const uint8_t *g, uint8_t c)
{
	int16_t r, py;
	uint8_t off = (uint8_t)((x & 3) << 1);

	for (r = 0; r < 8; r++) {
		uint8_t row = g[r], va, vb, pat, *p;
		uint32_t m;
		py = y + r;
		if (!row || py < 0 || py >= s->h) continue;
		/* the checker: even pixels take pa on even rows, as put() does */
		va = (py & 1) ? s->pb[c] : s->pa[c];
		vb = (py & 1) ? s->pa[c] : s->pb[c];
		pat = (uint8_t)((va << 6) | (vb << 4) | (va << 2) | vb);
		m = ((uint32_t)exp4[row >> 4] << 16 | (uint32_t)exp4[row & 15] << 8) >> off;
		p = rowp(s, py) + ((uint16_t)x >> 2);
		p[0] = (uint8_t)((p[0] & ~(m >> 16)) | (pat & (m >> 16)));
		p[1] = (uint8_t)((p[1] & ~(m >> 8)) | (pat & (m >> 8)));
		if (off) p[2] = (uint8_t)((p[2] & ~m) | (pat & m));
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
		if (s->lg == 1 && !big && x >= 0 && x + 8 <= s->w) {
			text2(s, x, y, g, c);
			continue;
		}
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

void gfx_copy(Surface *d, const Surface *src, int16_t x, int16_t y, int16_t w, int16_t h)
{
	int16_t x1 = x + w, y1 = y + h, j;
	uint16_t b0, b1, i;

	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x1 > d->w) x1 = d->w;
	if (y1 > d->h) y1 = d->h;
	if (x >= x1 || y >= y1 || src->rowBytes != d->rowBytes || src->h < y1) return;
	b0 = (uint16_t)(((uint16_t)x << d->lg) >> 3);
	b1 = (uint16_t)((((uint16_t)x1 << d->lg) + 7) >> 3);
	for (j = y; j < y1; j++) {
		uint8_t *p = rowp(d, j);
		const uint8_t *q = src->bits + MULU16(j, src->rowBytes);
		for (i = b0; i < b1; i++) p[i] = q[i];
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

/* 2-bit picture pixels at other depths, as the IIIx shows its greys (light
 * grey near white): 1 bit keeps only black, 4 bits and colour use those greys. */
static const uint8_t pic2to4[4] = { 0, 1, 3, 15 };
static const uint8_t pic2to8[4] = { 0, PAL_GREY + 7, PAL_GREY + 6, 255 };

/* One unpacked picture row at another depth (bd), or an 8-bit 320 wide row
 * halved onto a 160 wide 8-bit surface. 2-bit pictures are 160 wide, so at
 * scale 2 each pixel goes in twice across and the row twice down (y, y + 1). */
static void SEC_UI pic_row(Surface *s, int16_t y, const uint8_t *src, uint16_t rb, uint8_t bd)
{
	int16_t x, n, sc = s->scale;
	uint8_t *p = rowp(s, y);

	if (bd == 8) {
		n = (int16_t)(rb >> 1);
		for (x = 0; x < n && s->ox + x < s->w; x++) p[s->ox + x] = src[x << 1];
		return;
	}
	n = (int16_t)(rb << 2);
	if (sc == 2 && s->depth == 4 && !(s->ox & 1) && s->ox + 2 * n <= s->w) {
		/* a byte a pixel */
		uint8_t *q = p + (s->ox >> 1);
		for (x = 0; x < n; x++)
			*q++ = (uint8_t)(pic2to4[(src[x >> 2] >> (6 - ((x & 3) << 1))) & 3] * 0x11);
	} else {
		for (x = 0; x < n && s->ox + x * sc < s->w; x++) {
			uint8_t v = (uint8_t)((src[x >> 2] >> (6 - ((x & 3) << 1))) & 3);
			if (s->depth != 2) v = s->depth == 8 ? pic2to8[v] : s->depth == 4 ? pic2to4[v] : (uint8_t)(v == 3);
			put(s, p, (int16_t)(s->ox + x * sc), v);
			if (sc == 2 && s->ox + 2 * x + 1 < s->w) put(s, p, (int16_t)(s->ox + 2 * x + 1), v);
		}
	}
	if (sc == 2 && y + 1 < s->h) {
		/* the row again below, whole bytes from the one holding ox */
		uint8_t *q = p + s->rowBytes;
		uint16_t a = (uint16_t)(((uint16_t)s->ox << s->lg) >> 3);
		uint16_t e = (uint16_t)(((uint16_t)(s->ox + 2 * n > s->w ? s->w : s->ox + 2 * n) << s->lg) + 7) >> 3;
		for (; a < e; a++) q[a] = p[a];
	}
}

static int SEC_UI pic_band(Surface *s, const uint8_t *d, uint32_t len, uint8_t crop)
{
	uint16_t y0, rows, rb, col = 0, row = 0, ob;
	int16_t left = 0, width = 0;
	uint32_t i = 8;
	uint8_t *p, conv, bd, dbl, buf[480];

	if (len < 8) return -1;
	y0 = (uint16_t)((d[0] << 8) | d[1]);
	rows = (uint16_t)((d[2] << 8) | d[3]);
	rb = (uint16_t)((d[4] << 8) | d[5]);
	bd = d[6];
	ob = (uint16_t)(((uint16_t)s->ox << s->lg) >> 3);
	/* the picture's own depth goes straight in; 2 bits can go to 1, 4 or 8,
	 * and doubled at scale 2 (dbl), and 8 bits at 320 to 8 bits at 160 */
	dbl = (uint8_t)(bd == 2 && s->scale == 2);
	conv = bd != s->depth || dbl || (bd == 8 && s->scale == 1 && rb > (uint16_t)s->w);
	if (conv && !(bd == 2 && s->depth != 16) && !(bd == 8 && s->depth == 8 && s->scale == 1 && rb <= 320))
		return -1;
	if (crop) {
		int16_t right = s->w - s->ox;
		int32_t top = (int32_t)y0 + s->oy, bottom = top + rows;
		if (bd != 8 || s->depth != 8 || conv || !rb || rb > sizeof(buf)) return -1;
		left = s->ox < 0 ? -s->ox : 0;
		if (right > (int16_t)rb) right = (int16_t)rb;
		width = right - left;
		if (width <= 0 || top >= s->h || bottom <= 0) return 0;
		if (!left && width == (int16_t)rb && top >= 0 && bottom <= s->h) crop = 0;
	} else if (bd == 8 && conv) {
		y0 >>= 1;
		if (y0 + ((rows + 1) >> 1) + s->oy > s->h) return -1;
	} else if (conv) {
		if (rb > 320 || ((y0 + rows) << dbl) + s->oy > s->h) return -1;
	} else if (rb + ob > s->rowBytes || y0 + rows + s->oy > s->h) {
		return -1;
	}
	p = conv || crop ? buf : rowp(s, (int16_t)(y0 + s->oy)) + ob;
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
				if (crop) {
					int32_t y = (int32_t)y0 + row + s->oy;
					if (y >= 0 && y < s->h) {
						int16_t x;
						q = rowp(s, (int16_t)y) + (s->ox + left);
						for (x = 0; x < width; x++) q[x] = buf[left + x];
					}
				} else if (!conv) p += s->rowBytes;
				else if (bd == 2) pic_row(s, (int16_t)(((y0 + row) << dbl) + s->oy), buf, rb, bd);
				else if (!(row & 1)) pic_row(s, (int16_t)(y0 + (row >> 1) + s->oy), buf, rb, bd);
				row++;
			}
		}
	}
	return 0;
}

int gfx_pic_band(Surface *s, const uint8_t *d, uint32_t len)
{
	return pic_band(s, d, len, 0);
}

int gfx_pic_band_crop8(Surface *s, const uint8_t *d, uint32_t len)
{
	return pic_band(s, d, len, 1);
}

/* 4x4 ordered dither values, row by row. */
static const uint8_t dith4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

void gfx_swap_mask(Surface *a, Surface *b, int16_t y0, int16_t y1, uint8_t lo, uint8_t hi)
{
	uint8_t ppb = (uint8_t)(8 >> a->lg), d = (uint8_t)(1 << a->lg), r, k, j;
	uint16_t n = a->rowBytes >> 1, i;
	union { uint8_t b[4]; uint16_t h[2]; uint32_t w; } m[4];
	int16_t y;

	if (y0 < 0) y0 = 0;
	if (y1 > a->h) y1 = a->h;
	/* each pattern row's mask for 4 bytes, which covers the 4 pixel pattern at every depth */
	for (r = 0; r < 4; r++)
		for (k = 0; k < 4; k++) {
			uint8_t v = 0;
			for (j = 0; j < ppb; j++) {
				uint8_t dv = dith4[(r << 2) | ((k * ppb + j) & 3)];
				v = (uint8_t)(v << d);
				if (dv >= lo && dv < hi) v |= (uint8_t)((1 << d) - 1);
			}
			m[r].b[k] = v;
		}
	for (y = y0; y < y1; y++) {
		uint16_t *p = (uint16_t *)rowp(a, y), *q = (uint16_t *)rowp(b, y), x;
		const uint16_t *mh = m[y & 3].h;
		if (!m[y & 3].w) continue;
		if (!(n & 1)) {
			/* two words at a time: the 68000 moves 32 bits at even addresses */
			uint32_t *p4 = (uint32_t *)p, *q4 = (uint32_t *)q, x4, mw = m[y & 3].w;
			for (i = (uint16_t)(n >> 1); i; i--) {
				x4 = (*p4 ^ *q4) & mw;
				*p4++ ^= x4;
				*q4++ ^= x4;
			}
			continue;
		}
		for (i = 0; i < n; i++) {
			x = (uint16_t)((p[i] ^ q[i]) & mh[i & 1]);
			p[i] ^= x;
			q[i] ^= x;
		}
	}
}

static void (*to16_fn)(const struct To16Job *a);

void gfx_to16_use(void (*fn)(const struct To16Job *a))
{
	to16_fn = fn;
}

const uint16_t *gfx_pal565(void)
{
	return pal_565;
}

void gfx_to16_mask(const Surface *s, uint8_t *d, uint16_t rb, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t le,
                   uint8_t lo, uint8_t hi)
{
	To16 a;

	if (s->depth != 8) return;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > s->w) x1 = s->w;
	if (y1 > s->h) y1 = s->h;
	if (x1 <= x0 || y1 <= y0) return;
	a.bits = s->bits;
	a.d = d;
	a.pal = pal_565;
	a.dith = dith4;
	a.srb = s->rowBytes;
	a.drb = rb;
	a.x0 = x0;
	a.y0 = y0;
	a.x1 = x1;
	a.y1 = y1;
	a.le = le;
	a.mask = lo > 0 || hi < 16;
	a.lo = lo;
	a.hi = hi;
	if (to16_fn) to16_fn(&a);
	else to16_run(&a);
}

void gfx_to16(const Surface *s, uint8_t *d, uint16_t rb, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t le)
{
	gfx_to16_mask(s, d, rb, x, y, (int16_t)(x + w), (int16_t)(y + h), le, 0, 16);
}

/* The masks of mask_rows: for each pattern row, 4 bytes covering the 4
 * pixel pattern at every depth, set where the dither value is from lo up
 * to (not) hi. */
static void SEC_CORE mask_rows(uint8_t lg, uint8_t lo, uint8_t hi, uint8_t m[4][4])
{
	uint8_t ppb = (uint8_t)(8 >> lg), d = (uint8_t)(1 << lg), r, k, j;
	for (r = 0; r < 4; r++)
		for (k = 0; k < 4; k++) {
			uint8_t v = 0;
			for (j = 0; j < ppb; j++) {
				uint8_t dv = dith4[(r << 2) | ((k * ppb + j) & 3)];
				v = (uint8_t)(v << d);
				if (dv >= lo && dv < hi) v |= (uint8_t)((1 << d) - 1);
			}
			m[r][k] = v;
		}
}

void gfx_copy_mask(Surface *d, const Surface *s, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t lo, uint8_t hi)
{
	uint8_t m[4][4];
	int16_t y;
	uint16_t b0, b1, j;

	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > d->w) x1 = d->w;
	if (y1 > d->h) y1 = d->h;
	if (x1 <= x0 || y1 <= y0 || lo >= hi) return;
	mask_rows(d->lg, lo, hi, m);
	/* whole 4 byte groups, so the 32 bit loop lines up with the pattern */
	b0 = (uint16_t)((((uint16_t)x0 << d->lg) >> 3) & ~3);
	b1 = (uint16_t)(((((uint16_t)(x1 - 1) << d->lg) >> 3) | 3) + 1);
	if (b1 > d->rowBytes) b1 = d->rowBytes;
	for (y = y0; y < y1; y++) {
		uint8_t *p = (uint8_t *)rowp(d, y);
		const uint8_t *q = rowp(s, y), *mk = m[y & 3];
		if (!(mk[0] | mk[1] | mk[2] | mk[3])) continue;
		if (!(d->rowBytes & 3) && !(b1 & 3)) {
			union { uint8_t b[4]; uint32_t w; } mw;
			uint32_t *p4 = (uint32_t *)(p + b0);
			const uint32_t *q4 = (const uint32_t *)(q + b0);
			mw.b[0] = mk[0];
			mw.b[1] = mk[1];
			mw.b[2] = mk[2];
			mw.b[3] = mk[3];
			for (j = (uint16_t)((b1 - b0) >> 2); j; j--, p4++, q4++) *p4 ^= (*p4 ^ *q4) & mw.w;
			continue;
		}
		for (j = b0; j < b1; j++) p[j] = (uint8_t)(p[j] ^ ((p[j] ^ q[j]) & mk[j & 3]));
	}
}
