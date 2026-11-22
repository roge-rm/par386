#include "terra.h"
#include "hole.h"
#include "pal.h"

/* Work counts for the host tests; left out on the Palm, where they'd cost time in the inner loops. */
#ifdef __palmos__
#define COUNT(e)
#else
#define COUNT(e) (e)
#endif

void terra_curve_shade(const TerraCurveShade *t)
{
	const uint8_t *cmd = t->spans;
	uint16_t n;

	for (n = 0; n < t->count; n++, cmd += CURVE_SPAN_BYTES) {
		uint16_t y = (uint16_t)((cmd[0] << 8) | cmd[1]);
		uint16_t x = (uint16_t)((cmd[2] << 8) | cmd[3]);
		uint16_t end = (uint16_t)((cmd[4] << 8) | cmd[5]);
		const uint8_t *pattern = t->bayer + (y & 3) * 4;
		uint8_t *row = t->bits + MULU16(y, t->rb);
		const int8_t *near = t->light + (y >> CURVE_LIGHT_SHIFT) * CURVE_LIGHT_STRIDE;
		const int8_t *far = near + CURVE_LIGHT_STRIDE;
		int16_t yf = (int16_t)(y & 15), sf = cmd[6];
		int16_t base = PAL_TERRAIN + sf * PAL_RAMP_N;
		int16_t mid = sf == SURF_FRINGE || sf == SURF_WATER ? 48 : 64;
		int textured = sf == SURF_GREEN || sf == SURF_FRINGE || sf == SURF_BUNKER;
		uint16_t grain_shift = sf == SURF_FRINGE;

		while (x < end) {
			uint16_t cell = x >> CURVE_LIGHT_SHIFT;
			uint16_t stop = (uint16_t)((cell + 1) << CURVE_LIGHT_SHIFT);
			int16_t a = (int16_t)(near[cell] * (16 - yf) + far[cell] * yf);
			int16_t b = (int16_t)(near[cell + 1] * (16 - yf) + far[cell + 1] * yf);
			int16_t light, step;
			if (stop > end) stop = end;
			if (sf == SURF_GREEN) {
				a /= 2;
				b /= 2;
			}
			step = (int16_t)(b - a);
			light = (int16_t)(mid * 256 + a * 16 + step * (x & 15));
			if (textured) {
				/* The collar has a wider grain than the green or sand. */
				const int8_t *grain = t->noise + ((((y >> grain_shift) + cell * 5) & 15) << 4);
				for (; x < stop; x++, light += step) {
					int16_t fleck = (grain[(x >> grain_shift) & 15] + 1) * (1 << grain_shift);
					row[x] = (uint8_t)(base + ((light + (15 - pattern[x & 3] + fleck) * 256) >> 12));
				}
			} else {
				for (; x < stop; x++, light += step)
					row[x] = (uint8_t)(base + ((light + (15 - pattern[x & 3]) * 256) >> 12));
			}
		}
	}
}

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

/* Inlined on the 68k, where gcc 2.95 won't by itself; the ARM build stays as it is. */
#ifdef __m68k__
#define INL68 __inline__
#else
#define INL68
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
static INL68 int16_t SEC_CORE dz6(int32_t camb, int32_t hb)
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

static inline SEC_CORE uint8_t surf_cell(const uint8_t *row, uint16_t x)
{
	uint8_t c = row[x >> 1];
	return (x & 1) ? (c & 15) : (c >> 4);
}

/* Cut a material corner where three cells agree on the other surface. */
static uint8_t SEC_CORE surface_corner(const uint8_t *row, uint16_t stride,
                                        uint16_t w, uint16_t h, int16_t cx, int16_t cy,
                                        uint8_t u, uint8_t v, uint8_t surf, uint8_t close)
{
	int16_t dx = u < 128 ? -1 : 1, dy = v < 128 ? -1 : 1;
	int16_t nx = cx + dx, ny = cy + dy;
	const uint8_t *other;
	uint8_t side;

	if ((uint16_t)nx >= w || (uint16_t)ny >= h) return surf;
	side = surf_cell(row, nx);
	if (side == surf || side > SURF_BUNKER) return surf;
	if (!close && (side >= SURF_GREEN || surf >= SURF_GREEN)) return surf;
	other = dy < 0 ? row - stride : row + stride;
	if (surf_cell(other, cx) == side && surf_cell(other, nx) == side) return side;
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

/* Surfaces with their own light in terra_row, a bit each; the rest (rough
 * and the like, the most common) take one test. */
#define SURF_SOFT ((1 << SURF_ROUGH) | (1 << SURF_FIRST_CUT) | (1 << SURF_DEEP_ROUGH) | \
                   (1 << SURF_FRINGE) | (1 << SURF_GREEN) | (1 << SURF_BUNKER))
#define SURF_OWN ((1 << SURF_FAIRWAY) | (1 << SURF_GREEN) | (1 << SURF_TEE) | (1 << SURF_BUNKER) | (1 << SURF_WATER))

/* Carry the nearer material to the cut while its previous sample is visible. */
static void SEC_DANA cut_span(TerraRow *t, int16_t x, int16_t prior, int16_t now, int16_t light)
{
	uint8_t sf = t->prior_material[x], weight = t->cut[x];
	int16_t bottom = t->ybuf[x], drop = 0, y;

	if (sf >= SURF_COUNT || sf == SURF_WATER || sf != t->prev[x]) return;
	if (sf == SURF_BUNKER) {
		drop = (int16_t)t->prior_sand_px;
		if (t->sand_relief) drop = (int16_t)((MULU16(drop, t->prior_relief[x]) + 128) >> 8);
	}
	if ((prior >> 4) + drop != bottom) return;
	y = (int16_t)((MULS16(prior, 256 - weight) + MULS16(now, weight)) >> 12);
	/* The continuous sand surface meets the grass at its unlowered height. */
	if (sf == SURF_BUNKER && !t->sand_relief)
		y += (int16_t)((MULS16(drop, 256 - weight) + MULS16(t->sand_px, weight)) >> 8);
	if (y < 0) y = 0;
	if (y >= bottom) return;
	if (SURF_SOFT & (1 << sf)) light = (int16_t)(LIGHT_FLAT + ((light - LIGHT_FLAT) >> 2));
	if (sf == SURF_FRINGE) light -= 16;
	if (sf == SURF_BUNKER) light += 16;
	far_span(t, x, y, bottom, 0, light, (uint8_t)(PAL_TERRAIN + sf * PAL_RAMP_N), 0);
}

void terra_row(TerraRow *t)
{
	const uint8_t *bayer = t->bayer, *cells = t->cells;
	const uint8_t *material = t->material, *cut = t->cut, *relief = t->sand_relief;
	const int8_t *noise = t->noise, *sand_light = t->sand_light;
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
			uint8_t surf, prev, wf, profiled = 0;

			if (cw > 1 && ybuf[x + 1] > yb) yb = ybuf[x + 1];
			if (yb > ymax) ymax = yb;
			if (cut) {
				uint8_t *history = t->prior_y + 2 * x;
				if (cut[x] && yb > 0) {
					int16_t prior = (int16_t)((uint16_t)history[0] << 8 | history[1]);
					cut_span(t, x, prior, ya, la);
					yb = ybuf[x];
				}
				history[0] = (uint8_t)((uint16_t)ya >> 8);
				history[1] = (uint8_t)ya;
			}
			/* lowered ground (sand, water) only goes further down, so this hides it too */
			if (y >= yb) continue;

			if (material && material[x] < SURF_COUNT) {
				surf = material[x];
				profiled = relief != 0;
			} else {
				/* A little jitter softens the surface grid's edges. */
				fixed sx = px, sy = py;
				int16_t cx, cy;
				if (!material && ns < 8) {
					sx += (fixed)noise[(uint8_t)(x * 7 + step * 13)] * 1024;
					sy += (fixed)noise[(uint8_t)(x * 11 + step * 5 + 101)] * 1024;
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
					c = cells[MULU16(cy, gs) + (((uint32_t)cx & 0xFFFF) >> 1)];
					surf = (cx & 1) ? (c & 15) : (c >> 4);
					if (surf != SURF_WATER) surf = SURF_OOB;
				} else {
					const uint8_t *row = cells + MULU16(cy, gs);
					uint8_t c = row[(uint16_t)cx >> 1];
					surf = (cx & 1) ? (c & 15) : (c >> 4);
					if (surf >= SURF_COUNT) surf = SURF_OOB;
					if (!material && ns < 8 && surf <= SURF_BUNKER) {
						uint8_t u = (uint8_t)(sx >> (8 + SURF_CELL_SHIFT));
						uint8_t v = (uint8_t)(sy >> (8 + SURF_CELL_SHIFT));
						uint8_t a = u < 128 ? u : 255 - u, b = v < 128 ? v : 255 - v;
						/* A neighbour in the same byte can rule out the corner. */
						if (a + b < (surf < SURF_GREEN ? 128 : 96) &&
						    !((c >> 4) == (c & 15) && (cx & 1) != (u >> 7)))
							surf = surface_corner(row, gs, gw, gh, cx, cy, u, v, surf, a + b < 96);
					}
				}
			}
			/* water found only by the probe is drawn wmin rows tall, not the whole step */
			wf = 0;
			if (tq && surf != SURF_WATER && !t->wet[x] && probe_water(t, px, py)) {
				surf = SURF_WATER;
				wf = WF_CAP;
			}
			prev = prevs[x];
			if (surf != SURF_BUNKER) prevs[x] = surf;
			if (surf == SURF_BUNKER) {
				y += profiled ? (int16_t)((MULU16(sand_px, relief[x]) + 128) >> 8) : sand_px;
				if (y >= yb) continue;
				prevs[x] = surf;
			} else if (surf == SURF_WATER) {
				y += water_px;
				if (y >= yb) continue;
			}
			if (y < 0) y = 0;

			{
				int16_t lv = la, yy;
				uint8_t base = (uint8_t)(PAL_TERRAIN + surf * PAL_RAMP_N);

				if (SURF_SOFT & (1 << surf)) lv = (int16_t)(LIGHT_FLAT + ((lv - LIGHT_FLAT) >> 2));
				if (surf == SURF_FRINGE) lv -= 16;
				if (prev == SURF_BUNKER && surf < SURF_BUNKER && !profiled) lv += 24;   /* the far lip faces the camera */
				if (!(SURF_OWN & (1 << surf))) {
					if (prev == SURF_WATER && t->wmin) wf |= WF_EDGE;    /* and the far bank of water */
				} else if (surf == SURF_FAIRWAY) {
					lv += ((py >> 19) & 1) ? stripe : -stripe;    /* 8 yard mowing bands */
				} else if (surf == SURF_GREEN) {
					lv += ((py >> 18) & 1) ? stripe / 2 : -stripe / 2;    /* 4 yard bands */
				} else if (surf == SURF_TEE) {
					lv += ((py >> 17) & 1) ? stripe : -stripe;
				} else if (surf == SURF_BUNKER) {
					lv += 16;
					if (profiled) {
						if (sand_light) lv += sand_light[x];
					} else if (prev != SURF_BUNKER) lv = 28 + t->hl;    /* the near lip's shadow */
				} else if (surf == SURF_WATER) {
					/* darker near, lighter far where it shows the sky */
					lv = t->wlv + t->hl;
					if (t->wmin) wf |= WF_FAR;
					if (prev != SURF_WATER) {
						if (t->wmin) wf |= WF_EDGE;       /* far: a dark line along the near bank */
						else lv = 0;                      /* near: the bank's shadow */
					}
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


#ifdef __m68k__
/* Projected coordinates fit in ±8191 pixels. Split the 16.16
 * numerator into two word divides when the edge needs a slope. */
static int32_t SEC_CARD tri_slope(int16_t dx, uint16_t dy)
{
	uint32_t whole = 0, fraction, result;
	uint16_t magnitude;

	if (!dx) return 0;
	if (dy == 1) return (int32_t)dx * 65536L;
	if (dy == 2) return (int32_t)dx * 32768L;
	magnitude = (uint16_t)(dx < 0 ? -(int32_t)dx : dx);
	fraction = (uint32_t)magnitude << 16;
	if (magnitude >= dy) {
		/* DIVU packs the remainder above the 16-bit quotient. */
		whole = magnitude;
		__asm__ ("divu.w %1,%0" : "+d" (whole) : "d" (dy) : "cc");
		fraction = whole & 0xffff0000UL;
	}
	__asm__ ("divu.w %1,%0" : "+d" (fraction) : "d" (dy) : "cc");
	result = ((uint32_t)(uint16_t)whole << 16) | (uint16_t)fraction;
	return dx < 0 ? -(int32_t)result : (int32_t)result;
}
#else
#define tri_slope(dx, dy) DIV((int32_t)(dx) * 65536L, (int32_t)(dy))
#endif

typedef struct {
	int32_t origin, dx, dy;     /* 24.8 at screen (0, 0), and steps per pixel */
} TurfPlane;

typedef struct {
	int16_t dx1, dx2, dy1, dy2;
	int32_t area;
} TurfBasis;

typedef struct {
	TurfPlane light, u, v, depth;
	int32_t mow_origin;        /* Q12 yards, matching v */
	uint8_t coarse_x, coarse_y, fine_x, fine_y;
	uint8_t coarse, fine, mow;
} TurfPaint;

#define MOW_FADE_END 56           /* yards from the camera */
#define TURF_MIX_SHIFT 5
#define TURF_MIX_MAX (1 << TURF_MIX_SHIFT)

static int32_t SEC_CARD turf_step(int32_t n, int32_t d)
{
	/* Keep the divide in 32 bits on ARM. An almost edge-on face may
	 * project so much ground into one pixel that its texture is skipped. */
	while (n < -0x7fffffL || n > 0x7fffffL) {
		n /= 2;
		d /= 2;
		if (!d) return 65537L;
	}
	return DIV(n * 256L, d);
}

static int SEC_CARD turf_plane(TurfPlane *p, const TurfBasis *b, const TerraTri *q,
                                const int16_t *v)
{
	int32_t a = (int32_t)v[1] - v[0], c = (int32_t)v[2] - v[0];
	int32_t dx = turf_step(a * b->dy2 - c * b->dy1, b->area);
	int32_t dy = turf_step(c * b->dx1 - a * b->dx2, b->area);

	if (dx < -65536L || dx > 65536L || dy < -65536L || dy > 65536L) {
		p->dx = p->dy = 0;
		p->origin = ((int32_t)v[0] + v[1] + v[2]) * 85L;
		return 0;
	}
	p->dx = dx;
	p->dy = dy;
	p->origin = (int32_t)v[0] * 256L - dx * q->x[0] - dy * q->y[0];
	return 1;
}

static uint8_t SEC_CARD turf_fade(const TurfPaint *p, uint8_t sx, uint8_t sy)
{
	int32_t ux = p->u.dx < 0 ? -p->u.dx : p->u.dx;
	int32_t uy = p->u.dy < 0 ? -p->u.dy : p->u.dy;
	int32_t vx = p->v.dx < 0 ? -p->v.dx : p->v.dx;
	int32_t vy = p->v.dy < 0 ? -p->v.dy : p->v.dy;
	int32_t u = (ux > uy ? ux : uy) >> (sx - 4);
	int32_t v = (vx > vy ? vx : vy) >> (sy - 4);
	int16_t weight = (int16_t)(20 - (u > v ? u : v));

	return (uint8_t)(weight < 0 ? 0 : weight > 16 ? 16 : weight);
}

static int SEC_CARD turf_setup(TurfPaint *p, const TerraTri *q, const TerraTurf *t,
                               const int8_t *noise, int16_t mow_phase)
{
	TurfBasis b;
	int rough = q->base == PAL_ROUGH || q->base == PAL_DEEP_ROUGH;
	int collar = q->base == PAL_FRINGE;
	int short_grass = q->base == PAL_GREEN || collar;
	int u, v;

	b.dx1 = (int16_t)(q->x[1] - q->x[0]);
	b.dx2 = (int16_t)(q->x[2] - q->x[0]);
	b.dy1 = (int16_t)(q->y[1] - q->y[0]);
	b.dy2 = (int16_t)(q->y[2] - q->y[0]);
	b.area = MULS16(b.dx1, b.dy2) - MULS16(b.dx2, b.dy1);
	if (!b.area) return 0;

	turf_plane(&p->light, &b, q, t->light);
	if (q->base == PAL_SAND) return 1;
	u = turf_plane(&p->u, &b, q, t->u);
	v = turf_plane(&p->v, &b, q, t->v);
	/* Longer texels along the hole stay visible on the foreshortened
	 * ground. The less closely cut grass uses a larger grain. */
	p->coarse_x = (uint8_t)(short_grass ? 11 + collar : 12 + rough);
	p->coarse_y = (uint8_t)(p->coarse_x + 1);
	p->fine_x = (uint8_t)(9 + rough);
	p->fine_y = (uint8_t)(10 + rough);
	p->coarse = u && v && noise ? turf_fade(p, p->coarse_x, p->coarse_y) : 0;
	if (short_grass) p->coarse >>= collar ? 1 : 2;
	p->fine = p->coarse && !short_grass ? turf_fade(p, p->fine_x, p->fine_y) : 0;
	p->mow = v && q->base == PAL_FAIRWAY &&
	         (t->depth[0] < MOW_FADE_END || t->depth[1] < MOW_FADE_END || t->depth[2] < MOW_FADE_END);
	if (p->mow) {
		turf_plane(&p->depth, &b, q, t->depth);
		p->mow_origin = (int32_t)mow_phase * 4096;
	}
	return p->mow || p->coarse || p->light.origin || p->light.dx || p->light.dy;
}

static INL68 int16_t SEC_CARD turf_mottle(const int8_t *tex, int32_t u, int32_t v,
                                         uint8_t sx, uint8_t sy)
{
	uint16_t x = (uint16_t)((u >> sx) & 15), y = (uint16_t)((v >> sy) & 15);
	uint16_t nx = (uint16_t)((x + 1) & 15), ny = (uint16_t)((y + 1) & 15);
	int16_t fx = (int16_t)((u >> (sx - 3)) & 7), fy = (int16_t)((v >> (sy - 3)) & 7);
	int16_t a = (int16_t)(tex[(y << 4) + x] * (8 - fx) + tex[(y << 4) + nx] * fx);
	int16_t b = (int16_t)(tex[(ny << 4) + x] * (8 - fx) + tex[(ny << 4) + nx] * fx);

	return (int16_t)(((a * (8 - fy) + b * fy + 32) >> 6) + 1);
}

static void SEC_CARD turf_span(const TurfPaint *t, const int8_t *tex, const uint8_t *dither,
                               uint8_t *row, int16_t x, int16_t end, int16_t y,
                               int16_t vh, uint8_t base, int16_t lv)
{
	int32_t light = t->light.origin + t->light.dx * x + t->light.dy * y;
	int32_t u = t->u.origin + t->u.dx * x + t->u.dy * y;
	int32_t v = t->v.origin + t->v.dx * x + t->v.dy * y;
	int32_t depth = t->mow ? t->depth.origin + t->depth.dx * x + t->depth.dy * y : 0;
	int32_t depth_step = t->mow ? t->depth.dx : 0;
	int16_t grain = base == PAL_FRINGE ? 32 : base == PAL_GREEN ? 16 : 0;
	/* Fade the stretched grass mottle from halfway down the view. */
	int16_t close = (int16_t)((y - (vh >> 1)) >> 2);
	int16_t foreground = grain || close < 0 ? 0 : close > TURF_MIX_MAX ? TURF_MIX_MAX : close;

	for (; x <= end; x++) {
		int32_t level = lv + (light >> 8) + dither[x & 3];
		int16_t mix = foreground;
		if (t->mow) {
			int32_t near = MOW_FADE_END - (depth >> 8);
			if (near > 0) {
				/* Divide two-yard units by three. The even-band bias
				 * keeps signed world coordinates in the unsigned range. */
				uint16_t at = (uint16_t)(((v - t->mow_origin) >> 13) + 32766);
				int16_t tight = (MULU16(at, 43691U) >> 17) & 1 ?
				                MOW_MID - MOW_CONTRAST : MOW_MID + MOW_CONTRAST;
				int16_t mow_mix = (int16_t)(near > TURF_MIX_MAX ? TURF_MIX_MAX : near);
				level += ((tight - lv) * mow_mix) >> TURF_MIX_SHIFT;
				if (mow_mix > mix) mix = mow_mix;
			}
		}
		if (t->coarse && mix < TURF_MIX_MAX) {
			int16_t coarse = turf_mottle(tex, u, v, t->coarse_x, t->coarse_y);
			int16_t grass = (int16_t)((coarse * t->coarse * 5) >> 6);
			if (t->fine) {
				uint16_t ux = (uint16_t)((u >> t->fine_x) & 15);
				uint16_t vy = (uint16_t)((v >> t->fine_y) & 15);
				grass += (int16_t)(((tex[(vy << 4) + ux] + 1) * t->fine * 3) >> 6);
			}
			level += grass - ((grass * mix + 16) >> TURF_MIX_SHIFT);
		}
		if ((mix || grain) && tex) {
			/* Keep the grain fine on the short grass and near the ball. */
			uint16_t nx = (uint16_t)((x + 3 * (y >> 4)) & 15);
			uint16_t ny = (uint16_t)((y + 5 * (x >> 4)) & 15);
			level += ((tex[(ny << 4) + nx] + 1) * (mix + grain)) >> (TURF_MIX_SHIFT + 1);
		}
		level >>= 4;
		if (level < 0) level = 0;
		if (level >= PAL_RAMP_N) level = PAL_RAMP_N - 1;
		row[x] = (uint8_t)(base + level);
		light += t->light.dx;
		u += t->u.dx;
		v += t->v.dx;
		depth += depth_step;
	}
}

static void SEC_CARD sand_span(const TurfPlane *p, const int8_t *tex, const uint8_t *dither,
                               uint8_t *row, int16_t x, int16_t end, int16_t y, int16_t lv)
{
	int32_t light = p->origin + p->dx * x + p->dy * y + (int32_t)lv * 256;

	for (; x <= end; x++, light += p->dx) {
		int16_t grain = dither[x & 3];
		int16_t level;
		if (tex) {
			uint16_t nx = (uint16_t)((x + 3 * (y >> 4)) & 15);
			uint16_t ny = (uint16_t)((y + 5 * (x >> 4)) & 15);
			grain = 10 + tex[(ny << 4) + nx] * 2;
		}
		level = (int16_t)(((light >> 8) + grain) >> 4);
		if (level < 0) level = 0;
		if (level >= PAL_RAMP_N) level = PAL_RAMP_N - 1;
		row[x] = (uint8_t)(PAL_SAND + level);
	}
}

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
		const uint8_t *compact = lg == 1 ? tt->pat2 + ((uint16_t)(base + ((lv + 8) >> 4)) << 2) : 0;
		TurfPaint turf;
		int shaded = 0;

		ax = q->x[0]; ay = q->y[0];
		bx = q->x[1]; by = q->y[1];
		cx = q->x[2]; cy = q->y[2];
		/* a at the top, c at the bottom */
		if (by < ay) { tx = ax; ty = ay; ax = bx; ay = by; bx = tx; by = ty; }
		if (cy < ay) { tx = ax; ty = ay; ax = cx; ay = cy; cx = tx; cy = ty; }
		if (cy < by) { tx = bx; ty = by; bx = cx; by = cy; cx = tx; cy = ty; }
		y0 = ay; ym = by; y1 = cy;
		if (y1 <= y0 || y1 < 0 || y0 >= vh) continue;
		if (lg == 3 && tt->turf &&
		    ((base >= PAL_ROUGH && base <= PAL_SAND) || base == PAL_TEE))
			shaded = turf_setup(&turf, q, &tt->turf[i], tt->noise, tt->mow_phase);
		d2 = tri_slope(cx - ax, y1 - y0);
		xl = (int32_t)ax * 65536L;
		xr = xl;
		dl = ym > y0 ? tri_slope(bx - ax, ym - y0) : 0;
		dr = d2;
		/* rows above the view only move the edges on */
		y = y0;
		if (y < 0) {
			int16_t k = (int16_t)-y0, ks = k < ym - y0 ? k : (int16_t)(ym - y0);
			xl += dl * ks;
			xr += dr * k;
			if (ym < 0 && cy > ym) {
				dl = tri_slope(cx - bx, cy - ym);
				xl = (int32_t)bx * 65536L + dl * (-ym);
			}
			y = 0;
		}
		if (y1 > vh) y1 = vh;
		for (; y < y1; y++) {
			uint8_t *row;

			if (y == ym) {
				/* the short side turns at b */
				xl = (int32_t)bx * 65536L;
				dl = tri_slope(cx - bx, cy - ym);
			}
			xa = (int16_t)((xl + 32768L) >> 16);
			xb = (int16_t)((xr + 32768L) >> 16);
			if (xa > xb) { x = xa; xa = xb; xb = x; }
			if (xa < 0) xa = 0;
			if (xb > w - 1) xb = (int16_t)(w - 1);
			xl += dl;
			xr += dr;
			if (xa > xb) continue;
			row = tt->bits + MULU16(y, tt->rb);
			if (compact) {
				/* 2 bits: the level's pattern row, whole bytes in the middle */
				uint8_t pat = compact[y & 3], m;
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
				if (shaded && base == PAL_SAND)
					sand_span(&turf.light, tt->noise, tb, row, xa, xb, y, lv);
				else if (shaded) turf_span(&turf, tt->noise, tb, row, xa, xb, y, vh, base, lv);
				else {
					uint8_t *p = row + xa;
					for (x = xa; x <= xb; x++) {
						int16_t l = (int16_t)((lv + tb[x & 3]) >> 4);
						if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
						*p++ = (uint8_t)(base + l);
					}
				}
			} else {
				for (x = xa; x <= xb; x++) {
					int16_t l = (int16_t)((lv + bayer[((y & 3) << 2) | (x & 3)]) >> 4);
					if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
					PUT(tt, row + ((uint16_t)x >> (3 - lg)), x, y, (uint8_t)(base + l));
				}
			}
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
		const uint8_t *row = 0;
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
			const uint8_t *tb = ts->bayer + ((y & 3) << 2);
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

#ifdef __arm__
__attribute__((always_inline))
#endif
static __inline__ uint8_t SEC_CORE map_pattern(const uint8_t *cells, const uint8_t *patterns, uint8_t column, uint8_t blank)
{
	uint8_t surface;

	if (column == 0xFF) return blank;
	surface = cells[column >> 1];
	surface = (column & 1) ? (surface & 15) : (surface >> 4);
	return patterns[surface];
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
			for (x = 0; x < (int16_t)(((uint16_t)w << lg) >> 3); x++) p[x] = 0xFF;
			continue;
		}
		/* the far end at the top; sample each pixel's centre */
		cy = (t->len - (fixed)(y - top) * t->ypp - (t->ypp >> 1)) >> (16 + SURF_CELL_SHIFT);
		if (cy < 0) cy = 0;
		if (cy >= t->gh) cy = t->gh - 1;
		cells = t->cells + MULU16(cy, t->gs);
		if (lg == 1) {
			/* 2 bits: this row of each surface's pattern, a byte at a time */
			uint8_t pr[16], i;
			for (i = 0; i < SURF_COUNT; i++) pr[i] = t->pat2[((uint16_t)col[i] << 2) | (y & 3)];
			for (; i < 16; i++) pr[i] = pr[SURF_OOB];
			for (x = 0; x < w; x += 4, p++) {
				if (xcell[x] == 0xFF && xcell[x + 3] == 0xFF) {
					*p = 0xFF;
					continue;
				}
				*p = (uint8_t)((map_pattern(cells, pr, xcell[x], 0xFF) & 0xC0) |
				               (map_pattern(cells, pr, xcell[x + 1], 0xFF) & 0x30) |
				               (map_pattern(cells, pr, xcell[x + 2], 0xFF) & 0x0C) |
				               (map_pattern(cells, pr, xcell[x + 3], 0xFF) & 0x03));
			}
			continue;
		}
		if (lg == 2) {
			/* Precompute the four dither phases and write two pixels per byte. */
			uint8_t pr[4][16], blank[4], k, i;
			const uint8_t *phase = t->bayer + ((y & 3) << 2);
			uint16_t dark = MULU16(255 - t->grey[PAL_BLACK], 15);
			for (k = 0; k < 4; k++) blank[k] = (uint8_t)((dark + (phase[k] << 4)) >> 8);
			for (i = 0; i < SURF_COUNT; i++) {
				uint16_t shade = MULU16(255 - t->grey[col[i]], 15);
				for (k = 0; k < 4; k++) pr[k][i] = (uint8_t)((shade + (phase[k] << 4)) >> 8);
			}
			for (; i < 16; i++)
				for (k = 0; k < 4; k++) pr[k][i] = pr[k][SURF_OOB];
			for (x = 0; x < w; x += 2, p++) {
				uint8_t phase0 = (uint8_t)(x & 2), phase1 = (uint8_t)(phase0 + 1);
				uint8_t a = map_pattern(cells, pr[phase0], xcell[x], blank[phase0]);
				uint8_t b = map_pattern(cells, pr[phase1], xcell[x + 1], blank[phase1]);
				*p = (uint8_t)((a << 4) | b);
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
