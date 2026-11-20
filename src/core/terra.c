#include "terra.h"
#include "hole.h"
#include "pal.h"

/* Work counts for the host tests; left out on the Palm, where they'd cost time in the inner loops. */
#ifdef __palmos__
#define COUNT(e)
#else
#define COUNT(e) (e)
#endif

/* No switch statements here: the ARM compiler makes jump tables of
 * addresses, which the blob can't hold. */

/* Writes palette index c at (x, y); p points at the byte holding the pixel. As render.c's put. */
static void SEC_CORE tput(uint8_t lg, const uint8_t *pat2, const uint8_t *grey, const uint8_t *bayer,
                          uint8_t *p, int16_t x, int16_t y, uint8_t c)
{
	uint8_t m, v;
	uint16_t g;

	if (lg == 3) {
		*p = c;
	} else if (lg == 1) {
		m = (uint8_t)(0xC0 >> ((x & 3) << 1));
		*p = (uint8_t)((*p & ~m) | (pat2[((uint16_t)c << 2) | (y & 3)] & m));
	} else if (lg == 2) {
		g = (uint16_t)(255 - grey[c]);
		v = (uint8_t)((MULU16(g, 15) + (bayer[((y & 3) << 2) | (x & 3)] << 4)) >> 8);
		if (x & 1) *p = (uint8_t)((*p & 0xF0) | v);
		else *p = (uint8_t)((*p & 0x0F) | (v << 4));
	} else {
		/* the 2-bit pattern, made for screens where only black shows:
		 * black stays, dark grey is black on one pixel in 4, the rest white */
		v = (uint8_t)((pat2[((uint16_t)c << 2) | (y & 3)] >> (6 - ((x & 3) << 1))) & 3);
		m = (uint8_t)(0x80 >> (x & 7));
		if (v == 3 || (v == 2 && (x & y & 1))) *p |= m;
		else *p &= (uint8_t)~m;
	}
}

/* Divides by shifts on the ARM, which has no divide and can't call a
 * library from the blob; the 68k and the host divide as before. */
#ifdef __arm__
#define DIV(a, b) terra_div(a, b)
#else
#define DIV(a, b) ((a) / (b))
#endif

#define PUT(t, p, x, y, c) tput((t)->lg, (t)->pat2, (t)->grey, (t)->bayer, p, x, y, c)

/* a * q / 16384 for a 2.14 q, as fix_mulq14. */
static fixed SEC_CORE mulq(fixed a, int16_t q)
{
	uint32_t ua, r;
	uint16_t uq;
	int neg = 0;

	if (a < 0) { ua = 0u - (uint32_t)a; neg = 1; } else ua = (uint32_t)a;
	if (q < 0) { uq = (uint16_t)-q; neg ^= 1; } else uq = (uint16_t)q;
	r = (MULU16(ua >> 16, uq) << 2) + (MULU16(ua, uq) >> 14);
	return neg ? -(fixed)r : (fixed)r;
}

/* Camera height minus a height, both 8.8 byte steps, as 1/64 steps clamped to 16 bits. */
static int16_t SEC_CORE dz6(int32_t camb, int32_t hb)
{
	int32_t d = (camb - hb) >> 2;
	if (d > 32767) d = 32767;
	if (d < -32768) d = -32768;
	return (int16_t)d;
}

#define LIGHT_FLAT 62                 /* ramp position of flat ground, 16 per level */

/* Height and light at a point, for the terrain march: the row the ground
 * projects to in 1/16 pixels (clamped to 16 bits) and its slope light in
 * ramp steps of 16. */
static void SEC_CORE sample(const TerraRow *t, fixed px, fixed py, int16_t *y16, int16_t *lv)
{
	fixed gx = px >> t->shift, gy = py >> t->shift;
	uint16_t i, j, i1, j1, fx, fy, hw = t->hw;
	const uint8_t *r0, *r1;
	int32_t tp, b, hb, y;
	int16_t sx0, sx1, gxs, gys;
	uint8_t sh = t->sh;

	if (gx < 0) gx = 0;
	if (gx > t->mx) gx = t->mx;
	if (gy < 0) gy = 0;
	if (gy > t->my) gy = t->my;
	i = (uint16_t)(gx >> 16); fx = (uint16_t)((gx >> 8) & 255);
	j = (uint16_t)(gy >> 16); fy = (uint16_t)((gy >> 8) & 255);
	i1 = i + 1 < hw ? i + 1 : i;
	j1 = j + 1 < t->hh ? j + 1 : j;
	r0 = t->heights + MULU16(j, hw);
	r1 = t->heights + MULU16(j1, hw);
	sx0 = r0[i1] - r0[i];
	sx1 = r1[i1] - r1[i];
	tp = ((int32_t)r0[i] << 8) + MULS16(sx0, fx);
	b = ((int32_t)r1[i] << 8) + MULS16(sx1, fx);
	hb = tp + (MULS16((b - tp) >> 1, fy) >> 7);

	/* gradients in 1/16 byte steps per heightmap cell */
	gxs = (int16_t)(sx0 * 16 + (MULS16(sx1 - sx0, fy) >> 4));
	gys = (int16_t)((b - tp) >> 4);
	*lv = (int16_t)(LIGHT_FLAT + ((MULS16(gxs, t->ak) + MULS16(gys, t->bk)) >> 12));

	y = MULS16(dz6(t->camb, hb), t->m);
	if (sh >= 4) y >>= sh - 4;
	else if (y > 0x3FFFFFL) y = 0x3FFFFFFL;
	else if (y < -0x3FFFFFL) y = -0x3FFFFFFL;
	else y <<= 4 - sh;
	y += (int32_t)t->yh << 4;
	if (y > 16000) y = 16000;
	if (y < -16000) y = -16000;
	*y16 = (int16_t)y;
}

/* The surface at (sx, sy), 16.16 yards; off the hole it's out of bounds,
 * or water where a water edge carries on. */
static uint8_t SEC_CORE surf_at(const TerraRow *t, fixed sx, fixed sy)
{
	int16_t cx = (int16_t)(sx >> (16 + SURF_CELL_SHIFT)), cy = (int16_t)(sy >> (16 + SURF_CELL_SHIFT));
	uint8_t c, off = 0, surf;
	if (sx < 0) { cx = 0; off = 1; }
	else if ((uint16_t)cx >= t->gw) { cx = (int16_t)(t->gw - 1); off = 1; }
	if (sy < 0) { cy = 0; off = 1; }
	else if ((uint16_t)cy >= t->gh) { cy = (int16_t)(t->gh - 1); off = 1; }
	c = t->cells[MULU16(cy, t->gs) + ((uint16_t)cx >> 1)];
	surf = (cx & 1) ? (c & 15) : (c >> 4);
	if (surf >= SURF_COUNT || (off && surf != SURF_WATER)) surf = SURF_OOB;
	return surf;
}

/* Bits for a sample of water past the far water depth: found only by the
 * probe, and with a dark line along its bank. */
#define WF_CAP  1
#define WF_EDGE 2
#define WF_FAR  4    /* any far water */

/* Whether the probe point, tq of the way from the camera to (px, py), is water. */
static int SEC_CORE probe_water(const TerraRow *t, fixed px, fixed py)
{
	return surf_at(t, t->camx + mulq(px - t->camx, t->tq), t->camy + mulq(py - t->camy, t->tq)) == SURF_WATER;
}

/* A bank of far water: a dark line along the bottom row of the span ys..ye
 * in column xx, and xx + 1 if pair. */
static void SEC_CORE bank_line(const TerraRow *t, int16_t xx, int16_t ys, int16_t ye, uint8_t pair)
{
	uint8_t c = t->lg == 3 ? (uint8_t)(PAL_TERRAIN + SURF_WATER * PAL_RAMP_N) : (uint8_t)PAL_BLACK, *p;
	int16_t yy;
	for (yy = (int16_t)(ye - t->scale); yy < ye; yy++) {
		if (yy < ys) continue;
		p = t->bits + MULU16(yy, t->rb) + ((uint16_t)xx >> (3 - t->lg));
		PUT(t, p, xx, yy, c);
		if (pair) PUT(t, p, xx + 1, yy, c);
	}
}

/* A far span in column xx, and xx + 1 if pair, from y to ye: far water
 * (WF_FAR) is kept wmin rows tall from its near bank, taking them from the
 * ground beyond, and water found only by the probe is that tall rather
 * than the whole step; WF_EDGE puts a dark line along the bottom. Drawn as
 * the terrain loop would with no noise. */
static void SEC_CORE far_span(TerraRow *t, int16_t xx, int16_t y, int16_t ye, uint8_t pair,
                              int16_t lv, uint8_t base, uint8_t wf)
{
	uint16_t rb = t->rb;
	int16_t yy, hb = t->hb;
	uint8_t *p;

	if (wf & WF_FAR) {
		if (!t->wet[xx] && ((wf & WF_CAP) || ye - y < t->wmin)) {
			y = (int16_t)(ye - t->wmin);
			if (y < 0) y = 0;
		}
		t->wet[xx] = 1;
		if (pair) t->wet[xx + 1] = 1;
	}
	COUNT(t->spans++);
	COUNT(t->pixels += (ye - y) * (pair ? 2 : 1));
	t->ybuf[xx] = y;
	if (pair) t->ybuf[xx + 1] = y;
	p = t->bits + MULU16(y, rb) + ((uint16_t)xx >> (3 - t->lg));
	if (t->lg == 1) {
		uint8_t mk = (uint8_t)((pair ? 0xF0 : 0xC0) >> ((xx & 3) << 1)), nm = (uint8_t)~mk, hm = t->hm;
		const uint8_t *pb, *ph;
		int16_t l = (lv + 8) >> 4;
		if (l < 0) l = 0;
		if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
		pb = t->pat2 + ((uint16_t)(base + l) << 2);
		ph = t->pat2 + ((uint16_t)(PAL_HAZE + l) << 2);
		for (yy = y; yy < ye; yy++, p += rb) {
			uint8_t q = (uint8_t)(yy & 3);
			*p = (uint8_t)((*p & nm) | (((hm >> q) & 1 ? ph : pb)[q] & mk));
		}
	} else {
		uint8_t dith = t->lg == 3;
		for (yy = y; yy < ye; yy++, p += rb) {
			uint8_t tb = t->bayer[((yy & 3) << 2) | (xx & 3)], c;
			int16_t l = (lv + (dith ? tb : 8)) >> 4;
			if (l < 0) l = 0;
			if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
			c = (hb > ((tb * 7 + 3) & 15)) ? (uint8_t)(PAL_HAZE + l) : (uint8_t)(base + l);
			if (dith) *p = c;
			else PUT(t, p, xx, yy, c);
		}
	}
	if (wf & WF_EDGE) bank_line(t, xx, y, ye, pair);
}

/* One column's span of ground from row y down to row yb - 1, shaded from
 * level lv (ramp steps of 16) on ramp base. */
#define SHADE(c, xx) do { \
	uint8_t tb = bayer[((yy & 3) << 2) | (xx & 3)]; \
	int16_t l = lv + (dith ? tb : 8); \
	if (ns < 8) { \
		int16_t nz = noise[((yy & 15) << 4) | (xx & 15)]; \
		l += (dith ? nz * 3 : nz) >> ns; \
	} \
	l >>= 4; \
	if (l < 0) l = 0; \
	if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1; \
	c = (hb > ((tb * 7 + 3) & 15)) ? (uint8_t)(PAL_HAZE + l) : (uint8_t)(base + l); \
} while (0)

void terra_row(TerraRow *t)
{
	const uint8_t *bayer = t->bayer, *cells = t->cells;
	const int8_t *noise = t->noise;
	int16_t *ybuf = t->ybuf;
	uint8_t *prevs = t->prev;
	fixed px = t->px, py = t->py, spx = t->spx, spy = t->spy, dpxc = t->dpxc, dpyc = t->dpyc;
	int16_t x, xe, ya, la, yn, ln, ymax = 0, cw = t->cw, ns = t->ns, hb = t->hb, stripe = t->stripe;
	int16_t seg = (int16_t)(1 << t->kss), sand_px = t->sand_px, water_px = t->water_px, tq = t->tq;
	uint16_t rb = t->rb, gw = t->gw, gh = t->gh, gs = t->gs, step = t->step;
	uint8_t kss = t->kss, hm = t->hm, lg = t->lg, dith = lg == 3;

	sample(t, spx, spy, &ya, &la);
	la += t->hl;
	for (x = 0; x < t->w; ya = yn, la = ln) {
		int16_t yd, ld;
		spx += t->dpxs;
		spy += t->dpys;
		sample(t, spx, spy, &yn, &ln);
		ln += t->hl;
		yd = (int16_t)(((yn - ya) >> kss) * cw);
		ld = (int16_t)(((ln - la) >> kss) * cw);
		for (xe = x + seg; x < xe; x += cw, px += dpxc, py += dpyc, ya += yd, la += ld) {
			int16_t yb = ybuf[x], y = ya >> 4, k;
			uint8_t surf, prev, wf;

			if (cw > 1 && ybuf[x + 1] > yb) yb = ybuf[x + 1];
			if (yb > ymax) ymax = yb;
			/* lowered ground (sand, water) only goes further down, so this hides it too */
			if (y >= yb) continue;

			/* Surface, with the lookup shaken by up to half a yard near the camera so cell edges break up. */
			{
				fixed sx = px, sy = py;
				int16_t cx, cy;
				if (ns < 8) {
					sx += (fixed)noise[(x * 7 + step * 13) & 255] * 4096;
					sy += (fixed)noise[(x * 11 + step * 5 + 101) & 255] * 4096;
				}
				cx = (int16_t)(sx >> (16 + SURF_CELL_SHIFT));
				cy = (int16_t)(sy >> (16 + SURF_CELL_SHIFT));
				if (sx < 0 || sy < 0 || (uint16_t)cx >= gw || (uint16_t)cy >= gh) {
					/* off the hole: out of bounds, or the sea carrying on past a water edge */
					uint8_t c;
					if (sx < 0) cx = 0;
					else if ((uint16_t)cx >= gw) cx = (int16_t)(gw - 1);
					if (sy < 0) cy = 0;
					else if ((uint16_t)cy >= gh) cy = (int16_t)(gh - 1);
					c = cells[MULU16(cy, gs) + ((uint16_t)cx >> 1)];
					surf = (cx & 1) ? (c & 15) : (c >> 4);
					if (surf != SURF_WATER) surf = SURF_OOB;
				} else {
					uint8_t c = cells[MULU16(cy, gs) + ((uint16_t)cx >> 1)];
					surf = (cx & 1) ? (c & 15) : (c >> 4);
					if (surf >= SURF_COUNT) surf = SURF_OOB;
				}
			}
			/* water found only by the probe is drawn wmin rows tall, not the whole step */
			wf = 0;
			if (tq && surf != SURF_WATER && !t->wet[x] && probe_water(t, px, py)) {
				surf = SURF_WATER;
				wf = WF_CAP;
			}
			prev = prevs[x];
			prevs[x] = surf;
			if (surf == SURF_BUNKER) {
				y += sand_px;
				if (y >= yb) continue;
			} else if (surf == SURF_WATER) {
				y += water_px;
				if (y >= yb) continue;
			}
			if (y < 0) y = 0;

			{
				int16_t lv = la, yy;
				uint8_t base = (uint8_t)(PAL_TERRAIN + surf * PAL_RAMP_N);

				if (surf == SURF_FAIRWAY) {
					lv += ((py >> 19) & 1) ? stripe : -stripe;    /* 8 yard mowing bands */
				} else if (surf == SURF_GREEN) {
					lv += ((py >> 18) & 1) ? stripe : -stripe;    /* 4 yard bands */
				} else if (surf == SURF_TEE) {
					lv += ((py >> 17) & 1) ? stripe : -stripe;
				} else if (surf == SURF_BUNKER) {
					lv += 16;
					if (prev != SURF_BUNKER) lv = 4 + t->hl;    /* the near lip's shadow */
				} else if (surf == SURF_WATER) {
					/* darker near, lighter far where it shows the sky */
					lv = t->wlv + t->hl;
					if (t->wmin) wf |= WF_FAR;
					if (prev != SURF_WATER) {
						if (t->wmin) wf |= WF_EDGE;       /* far: a dark line along the near bank */
						else lv = 0;                      /* near: the bank's shadow */
					}
				} else {
					if (prev == SURF_BUNKER) lv += 24;   /* the far lip faces the camera */
					if (prev == SURF_WATER && t->wmin) wf |= WF_EDGE;    /* and the far bank of water */
				}
				if (cw > 1) prevs[x + 1] = surf;

				/* the span in each column this sample covers */
				for (k = 0; k < cw; k++) {
					int16_t xx = x + k, ye = ybuf[xx];
					uint8_t *p, pair = 0;
					if (y >= ye) continue;
					if (wf) {
						/* far water or its bank, drawn on its own to keep this loop short */
						if (!k && cw > 1 && lg == 1 && ybuf[xx + 1] == ye) {
							pair = 1;
							k++;
						}
						far_span(t, xx, y, ye, pair, lv, base, wf);
						continue;
					}
					COUNT(t->spans++);
					COUNT(t->pixels += ye - y);
					ybuf[xx] = y;
					/* at 2 bits a pair of columns with the same span is one byte's job */
					if (!k && cw > 1 && lg == 1 && ybuf[xx + 1] == ye) {
						pair = 1;
						k++;
						ybuf[xx + 1] = y;
						COUNT(t->pixels += ye - y);
					}
					p = t->bits + MULU16(y, rb) + ((uint16_t)xx >> (3 - lg));
					if (lg == 3) {
						for (yy = y; yy < ye; yy++, p += rb) {
							uint8_t c;
							SHADE(c, xx);
							*p = c;
						}
					} else if (lg == 1) {
						/* 2 bits: the level isn't dithered, so a span is one colour
						 * a row of the pattern, but for noise near and haze far */
						uint8_t mk = (uint8_t)((pair ? 0xF0 : 0xC0) >> ((xx & 3) << 1)), nm = (uint8_t)~mk;
						if (ns < 8) {
							const int8_t *nzc = noise + (xx & 15);
							const uint8_t *pat = t->pat2 + ((uint16_t)base << 2);
							int16_t l0 = lv + 8;
							if (l0 >= 8 && l0 <= PAL_RAMP_N * 16 - 8) {
								/* the noise (-8 to 7) can't take the level off the ramp: no clamps */
								const int8_t *nzp = nzc + ((y & 15) << 4), *nze = nzc + 256;
								for (yy = y; yy < ye; yy++, p += rb) {
									int16_t l = (l0 + (*nzp >> ns)) >> 4;
									*p = (uint8_t)((*p & nm) | (pat[(l << 2) | (yy & 3)] & mk));
									nzp += 16;
									if (nzp >= nze) nzp -= 256;
								}
							} else for (yy = y; yy < ye; yy++, p += rb) {
								int16_t l = (l0 + (nzc[(yy & 15) << 4] >> ns)) >> 4;
								if (l < 0) l = 0;
								if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
								*p = (uint8_t)((*p & nm) | (pat[(l << 2) | (yy & 3)] & mk));
							}
						} else if (hm == 0 || hm == 15) {
							const uint8_t *pat;
							int16_t l = (lv + 8) >> 4;
							if (l < 0) l = 0;
							if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
							pat = t->pat2 + ((uint16_t)((hm ? PAL_HAZE : base) + l) << 2);
							for (yy = y; yy < ye; yy++, p += rb)
								*p = (uint8_t)((*p & nm) | (pat[yy & 3] & mk));
						} else {
							/* haze a whole pattern row at a time */
							const uint8_t *pb, *ph;
							int16_t l = (lv + 8) >> 4;
							if (l < 0) l = 0;
							if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
							pb = t->pat2 + ((uint16_t)(base + l) << 2);
							ph = t->pat2 + ((uint16_t)(PAL_HAZE + l) << 2);
							for (yy = y; yy < ye; yy++, p += rb) {
								uint8_t q = (uint8_t)(yy & 3);
								*p = (uint8_t)((*p & nm) | (((hm >> q) & 1 ? ph : pb)[q] & mk));
							}
						}
					} else {
						for (yy = y; yy < ye; yy++, p += rb) {
							uint8_t c;
							SHADE(c, xx);
							PUT(t, p, xx, yy, c);
						}
					}
				}
			}
		}
	}
	t->ymax = ymax;
}
#undef SHADE


void terra_tris(const TerraTris *tt)
{
	const uint8_t *bayer = tt->bayer;
	int16_t i, vh = tt->vh, w = tt->w;
	uint8_t lg = tt->lg;

	for (i = 0; i < tt->n; i++) {
		const TerraTri *q = &tt->tri[i];
		int32_t xl, xr, dl, dr, d2;
		int16_t y, y0, ym, y1, x, xa, xb, ax, ay, bx, by, cx, cy, tx, ty, lv = q->lv;
		uint8_t base = (uint8_t)q->base;

		ax = q->x[0]; ay = q->y[0];
		bx = q->x[1]; by = q->y[1];
		cx = q->x[2]; cy = q->y[2];
		/* a at the top, c at the bottom */
		if (by < ay) { tx = ax; ty = ay; ax = bx; ay = by; bx = tx; by = ty; }
		if (cy < ay) { tx = ax; ty = ay; ax = cx; ay = cy; cx = tx; cy = ty; }
		if (cy < by) { tx = bx; ty = by; bx = cx; by = cy; cx = tx; cy = ty; }
		y0 = ay; ym = by; y1 = cy;
		if (y1 <= y0 || y1 < 0 || y0 >= vh) continue;
		d2 = DIV((int32_t)(cx - ax) * 65536L, (int32_t)(y1 - y0));
		xl = (int32_t)ax * 65536L;
		xr = xl;
		dl = ym > y0 ? DIV((int32_t)(bx - ax) * 65536L, (int32_t)(ym - y0)) : 0;
		dr = d2;
		/* rows above the view only move the edges on */
		y = y0;
		if (y < 0) {
			int16_t k = (int16_t)-y0, ks = k < ym - y0 ? k : (int16_t)(ym - y0);
			xl += dl * ks;
			xr += dr * k;
			if (ym < 0 && cy > ym) {
				dl = DIV((int32_t)(cx - bx) * 65536L, (int32_t)(cy - ym));
				xl = (int32_t)bx * 65536L + dl * (-ym);
			}
			y = 0;
		}
		if (y1 > vh) y1 = vh;
		for (; y < y1; y++) {
			if (y == ym) {
				/* the short side turns at b */
				xl = (int32_t)bx * 65536L;
				dl = DIV((int32_t)(cx - bx) * 65536L, (int32_t)(cy - ym));
			}
			if (y >= 0 && y < vh) {
				uint8_t *row = tt->bits + MULU16(y, tt->rb);
				xa = (int16_t)((xl + 32768L) >> 16);
				xb = (int16_t)((xr + 32768L) >> 16);
				if (xa > xb) { x = xa; xa = xb; xb = x; }
				if (xa < 0) xa = 0;
				if (xb > w - 1) xb = (int16_t)(w - 1);
				if (lg == 1 && xa <= xb) {
					/* 2 bits: the level's pattern row, whole bytes in the middle */
					uint8_t pat = tt->pat2[((uint16_t)(base + ((lv + 8) >> 4)) << 2) | (y & 3)], m;
					uint8_t *p = row + (xa >> 2), *pe = row + (xb >> 2);
					m = (uint8_t)(0xFF >> ((xa & 3) << 1));
					if (p == pe) m &= (uint8_t)(0xFF << ((3 - (xb & 3)) << 1));
					*p = (uint8_t)((*p & ~m) | (pat & m));
					if (p != pe) {
						for (p++; p < pe; p++) *p = pat;
						m = (uint8_t)(0xFF << ((3 - (xb & 3)) << 1));
						*p = (uint8_t)((*p & ~m) | (pat & m));
					}
				} else if (lg == 3) {
					const uint8_t *tb = bayer + ((y & 3) << 2);
					uint8_t *p = row + xa;
					for (x = xa; x <= xb; x++) {
						int16_t l = (int16_t)((lv + tb[x & 3]) >> 4);
						if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
						*p++ = (uint8_t)(base + l);
					}
				} else {
					for (x = xa; x <= xb; x++) {
						int16_t l = (int16_t)((lv + bayer[((y & 3) << 2) | (x & 3)]) >> 4);
						if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
						PUT(tt, row + ((uint16_t)x >> (3 - lg)), x, y, (uint8_t)(base + l));
					}
				}
			}
			xl += dl;
			xr += dr;
		}
	}
}

/* Puts the part of a PackBits run over k..e that's in lo..hi into out:
 * the literal bytes from src, or the byte *src repeated (rep). */
static void SEC_CORE put_run(uint8_t *out, const uint8_t *src, uint8_t rep, uint16_t k, uint16_t e,
                             uint16_t lo, uint16_t hi)
{
	uint16_t a = k > lo ? k : lo, b = e < hi ? e : hi;
	if (rep) while (a < b) out[a++] = *src;
	else for (; a < b; a++) out[a] = src[a - k];
}

/* One PackBits row of n bytes from d at *pos, putting in out only the
 * bytes from lo to hi (hi may be past n, carrying on from 0). Returns -1
 * if cut short. */
static int SEC_CORE unpack_row(const uint8_t *d, uint32_t len, uint32_t *pos, uint8_t *out,
                               uint16_t n, uint16_t lo, uint16_t hi)
{
	uint32_t i = *pos;
	uint16_t k = 0, wrap = hi > n ? (uint16_t)(hi - n) : 0;

	if (hi > n) hi = n;
	while (k < n) {
		int8_t c;
		uint16_t cnt, e;
		if (i >= len) return -1;
		c = (int8_t)d[i++];
		if (c == -128) continue;
		cnt = (uint16_t)(c >= 0 ? c + 1 : 1 - c);
		e = (uint16_t)(k + cnt);
		if (e > n || (c >= 0 ? i + cnt > len : i >= len)) return -1;
		if (e > lo && k < hi) put_run(out, d + i, c < 0, k, e, lo, hi);
		if (k < wrap) put_run(out, d + i, c < 0, k, e, 0, wrap);
		i += c >= 0 ? cnt : 1;
		k = e;
	}
	*pos = i;
	return 0;
}

void terra_sky(TerraSky *ts)
{
	const int16_t *ybuf = ts->ybuf;
	uint32_t pos = ts->pos;
	int16_t x, y, w = ts->w, yh = ts->yh, off = ts->off, bw = ts->bw, bh = ts->bh, btop = ts->btop;
	uint8_t lg = ts->lg, dith = lg == 3;

	for (y = ts->y0; y < ts->y1; y++) {
		const uint8_t *row = 0, *tb = ts->bayer + ((y & 3) << 2);
		uint8_t *p = ts->bits + MULU16(y, ts->rb);
		int16_t sv = y < yh ? (int16_t)DIV((int32_t)y * (16 * (PAL_SKY_N - 1)), (int32_t)yh) : 16 * (PAL_SKY_N - 1);
		int16_t sx = off;
		if (bw && y >= btop && y < btop + bh) {
			if (unpack_row(ts->bk, ts->len, &pos, ts->row, (uint16_t)bw, (uint16_t)(bw > w ? off : 0), (uint16_t)(bw > w ? off + w : bw)) == 0) row = ts->row;
			else bw = 0;
		}
		if (lg == 1) {
			/* 2 bits: the sky is one colour a row, so its pattern byte goes in
			 * whole where all 4 pixels are sky and there's no backdrop */
			uint8_t c = y <= yh ? (uint8_t)(PAL_SKY + ((sv + 8) >> 4)) : (uint8_t)(PAL_HAZE + 4);
			const uint8_t *pat = ts->pat2 + (y & 3);
			uint8_t b = pat[(uint16_t)c << 2];
			const int16_t *yb = ybuf;
			for (x = 0; x < w; x += 4, yb += 4, p++) {
				if (!row && y < yb[0] && y < yb[1] && y < yb[2] && y < yb[3]) {
					*p = b;
					COUNT(ts->px += 4);
				} else {
					uint8_t k, v = *p, mk = 0xC0;
					for (k = 0; k < 4; k++, mk >>= 2) {
						if (y < yb[k]) {
							uint8_t pc = b;
							if (row && row[sx]) pc = pat[(uint16_t)row[sx] << 2];
							v = (uint8_t)((v & ~mk) | (pc & mk));
							COUNT(ts->px++);
						}
						if (++sx >= bw) sx = 0;
					}
					*p = v;
					continue;
				}
				sx += 4;
				while (sx >= bw && bw) sx -= bw;
			}
			continue;
		}
		for (x = 0; x < w; x++) {
			uint8_t c = 0;
			if (y < ybuf[x]) {
				if (row) c = row[sx];
				if (!c) {
					if (y <= yh) {
						int16_t l = (sv + (dith ? tb[x & 3] : 8)) >> 4;
						c = (uint8_t)(PAL_SKY + (l > PAL_SKY_N - 1 ? PAL_SKY_N - 1 : l));
					} else {
						c = PAL_HAZE + 4;
					}
				}
				if (dith) p[x] = c;
				else PUT(ts, p + ((uint16_t)x >> (3 - lg)), x, y, c);
				COUNT(ts->px++);
			}
			if (++sx >= bw) sx = 0;
		}
	}
	ts->pos = pos;
	ts->bw = bw;
}

void terra_map(const TerraMap *t)
{
	const uint8_t *xcell = t->xcell, *col = t->col;
	uint16_t rb = t->rb;
	int16_t x, y, w = t->w, top = t->top, mh = t->mh;
	uint8_t lg = t->lg;

	for (y = t->y0; y < t->y1; y++) {
		uint8_t *p = t->bits + MULU16(y, rb);
		int32_t cy;
		const uint8_t *cells;
		if (y < top || y >= top + mh) {
			for (x = 0; x < (int16_t)rb; x++) p[x] = 0xFF;
			continue;
		}
		/* the far end at the top; sample each pixel's centre */
		cy = (t->len - (fixed)(y - top) * t->ypp - (t->ypp >> 1)) >> (16 + SURF_CELL_SHIFT);
		if (cy < 0) cy = 0;
		if (cy >= t->gh) cy = t->gh - 1;
		cells = t->cells + MULU16(cy, t->gs);
		if (lg == 1) {
			/* 2 bits: this row of each surface's pattern, a byte at a time */
			uint8_t pr[SURF_COUNT + 1], i;
			for (i = 0; i < SURF_COUNT; i++) pr[i] = t->pat2[((uint16_t)col[i] << 2) | (y & 3)];
			pr[SURF_COUNT] = 0xFF;
			for (x = 0; x < w; x += 4, p++) {
				uint8_t k, v = 0;
				if (xcell[x] == 0xFF && xcell[x + 3] == 0xFF) {
					*p = 0xFF;
					continue;
				}
				for (k = 0; k < 4; k++) {
					uint8_t cx = xcell[x + k], c = SURF_COUNT;
					if (cx != 0xFF) {
						c = cells[cx >> 1];
						c = (cx & 1) ? (c & 15) : (c >> 4);
						if (c >= SURF_COUNT) c = SURF_OOB;
					}
					v |= (uint8_t)(pr[c] & (0xC0 >> (k << 1)));
				}
				*p = v;
			}
			continue;
		}
		for (x = 0; x < w; x++) {
			uint8_t c = PAL_BLACK, cx = xcell[x];
			if (cx != 0xFF) {
				uint8_t v = cells[cx >> 1];
				v = (cx & 1) ? (v & 15) : (v >> 4);
				c = col[v < SURF_COUNT ? v : SURF_OOB];
			}
			if (lg == 3) p[x] = c;
			else PUT(t, p + ((uint16_t)x >> (3 - lg)), x, y, c);
		}
	}
}
