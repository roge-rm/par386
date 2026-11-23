/* The ARM code for OS 5's renderer loops, the `armc' 1001 resource called
 * through PceNativeCall: terrain, triangles, sky, maps and putting spans
 * (src/core/terra.c) as native code. The 68k passes its jobs as they lie in
 * its memory, big endian, so the entry reads each field byte by byte into
 * a copy, swaps the y-buffer to the ARM's byte order for the call and back
 * after, and writes back what the job returns. Built for the host too,
 * for the tests.
 *
 * Like armc 1000, the blob runs where it lies: no globals, no tables
 * reached by address, no switch tables and no library calls (the Makefile
 * checks the relocations). terraarm_entry has to come first. */
#include "terra.h"
#include "terraarm.h"
#include "pal.h"

static uint32_t be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t be16(const uint8_t *p)
{
	return (uint16_t)((p[0] << 8) | p[1]);
}

#define P32(b, o) ((uintptr_t)be32((b) + (o)))
#define S16(b, o) ((int16_t)be16((b) + (o)))

/* Swaps n 16-bit values in place. */
static void swap16(int16_t *v, int16_t n)
{
	uint8_t *p = (uint8_t *)v, c;
	int16_t i;
	for (i = 0; i < n; i++, p += 2) {
		c = p[0];
		p[0] = p[1];
		p[1] = c;
	}
}

static void row(uint8_t *b)
{
	TerraRow t;

	t.bits = (uint8_t *)P32(b, TR_BITS);
	t.heights = (const uint8_t *)P32(b, TR_HEIGHTS);
	t.cells = (const uint8_t *)P32(b, TR_CELLS);
	t.ybuf = (int16_t *)P32(b, TR_YBUF);
	t.prev = (uint8_t *)P32(b, TR_PREV);
	t.wet = (uint8_t *)P32(b, TR_WET);
	t.bayer = (const uint8_t *)P32(b, TR_BAYER);
	t.noise = (const int8_t *)P32(b, TR_NOISE);
	t.pat2 = (const uint8_t *)P32(b, TR_PAT2);
	t.grey = (const uint8_t *)P32(b, TR_GREY);
	t.camx = (fixed)be32(b + TR_CAMX);
	t.camy = (fixed)be32(b + TR_CAMY);
	t.camb = (int32_t)be32(b + TR_CAMB);
	t.mx = (fixed)be32(b + TR_MX);
	t.my = (fixed)be32(b + TR_MY);
	t.px = (fixed)be32(b + TR_PX);
	t.py = (fixed)be32(b + TR_PY);
	t.dpxc = (fixed)be32(b + TR_DPXC);
	t.dpyc = (fixed)be32(b + TR_DPYC);
	t.spx = (fixed)be32(b + TR_SPX);
	t.spy = (fixed)be32(b + TR_SPY);
	t.dpxs = (fixed)be32(b + TR_DPXS);
	t.dpys = (fixed)be32(b + TR_DPYS);
	t.rb = be16(b + TR_RB);
	t.w = S16(b, TR_W);
	t.yh = S16(b, TR_YH);
	t.hw = be16(b + TR_HW);
	t.hh = be16(b + TR_HH);
	t.gw = be16(b + TR_GW);
	t.gh = be16(b + TR_GH);
	t.gs = be16(b + TR_GS);
	t.ak = S16(b, TR_AK);
	t.bk = S16(b, TR_BK);
	t.hl = S16(b, TR_HL);
	t.wmin = S16(b, TR_WMIN);
	t.tq = S16(b, TR_TQ);
	t.m = S16(b, TR_M);
	t.cw = S16(b, TR_CW);
	t.ns = S16(b, TR_NS);
	t.sand_px = S16(b, TR_SAND);
	t.water_px = S16(b, TR_WATER);
	t.hb = S16(b, TR_HB);
	t.step = be16(b + TR_STEP);
	t.stripe = S16(b, TR_STRIPE);
	t.wlv = S16(b, TR_WLV);
	t.lg = b[TR_LG];
	t.scale = b[TR_SCALE];
	t.shift = b[TR_SHIFT];
	t.sh = b[TR_SH];
	t.kss = b[TR_KSS];
	t.hm = b[TR_HM];
	t.material = (const uint8_t *)P32(b, TR_MATERIAL);
	t.cut = (const uint8_t *)P32(b, TR_CUT);
	t.prior_material = (const uint8_t *)P32(b, TR_PRIOR_MATERIAL);
	t.prior_y = (uint8_t *)P32(b, TR_PRIOR_Y);
	t.prior_sand_px = (int32_t)be32(b + TR_PRIOR_SAND);
	t.sand_relief = (const uint8_t *)P32(b, TR_SAND_RELIEF);
	t.sand_light = (const int8_t *)P32(b, TR_SAND_LIGHT);
	t.prior_relief = (const uint8_t *)P32(b, TR_PRIOR_RELIEF);
	t.spans = t.pixels = 0;
	swap16(t.ybuf, t.w);
	terra_row(&t);
	swap16(t.ybuf, t.w);
	b[TR_YMAX] = (uint8_t)((uint16_t)t.ymax >> 8);
	b[TR_YMAX + 1] = (uint8_t)t.ymax;
}

static void tri_setup(TerraTris *t, const uint8_t *b)
{
	t->bits = (uint8_t *)P32(b, TT_BITS);
	t->bayer = (const uint8_t *)P32(b, TT_BAYER);
	t->pat2 = (const uint8_t *)P32(b, TT_PAT2);
	t->grey = (const uint8_t *)P32(b, TT_GREY);
	t->rb = be16(b + TT_RB);
	t->w = S16(b, TT_W);
	t->vh = S16(b, TT_VH);
	t->n = S16(b, TT_N);
	t->lg = b[TT_LG];
	t->shade_grass = b[TT_SHADE_GRASS];
	t->noise = (const int8_t *)P32(b, TT_NOISE);
	t->mow_phase = S16(b, TT_MOW_PHASE);
	t->reflection = (const uint8_t *)P32(b, TT_REFLECTION);
	t->green_u = S16(b, TT_GREEN_U);
	t->green_v = S16(b, TT_GREEN_V);
	t->turf = 0;
	if (t->n < 0) t->n = 0;
	if (t->n > TERRA_MESH_TRIS) t->n = TERRA_MESH_TRIS;
}

static void tris(const uint8_t *b, const int8_t *mottle)
{
	TerraTris t;
	TerraTri tl[TERRA_MESH_TRIS];
	TerraTurf turf[TERRA_MESH_TRIS];
	const uint8_t *q = (const uint8_t *)P32(b, TT_TRI);
	int16_t i, k;

	tri_setup(&t, b);
	/* the triangles, 8 big endian 16-bit fields each: x[3], y[3], base, lv */
	for (i = 0; i < t.n; i++, q += 16) {
		for (k = 0; k < 3; k++) {
			tl[i].x[k] = S16(q, 2 * k);
			tl[i].y[k] = S16(q, 6 + 2 * k);
		}
		tl[i].base = S16(q, 12);
		tl[i].lv = S16(q, 14);
	}
	t.tri = tl;
	q = (const uint8_t *)P32(b, TT_TURF);
	if (q && t.lg == 3) {
		for (i = 0; i < t.n; i++, q += TT_TURF_SIZE) {
			int16_t base = tl[i].base;
			int grass = t.shade_grass && base >= PAL_ROUGH && base <= PAL_DEEP_ROUGH;
			uint8_t material = (uint8_t)((base - PAL_TERRAIN) / PAL_RAMP_N);

			if ((base < PAL_ROUGH || base > PAL_SAND) && base != PAL_TEE) continue;
			for (k = 0; k < 3; k++) {
				turf[i].u[k] = S16(q, 2 * k);
				turf[i].v[k] = S16(q, 6 + 2 * k);
				turf[i].light[k] = S16(q, 12 + 2 * k);
				turf[i].depth[k] = S16(q, 18 + 2 * k);
				if (grass)
					turf[i].light[k] = mesh_grass_light(turf[i].u[k], turf[i].v[k],
					                                    turf[i].light[k], turf[i].depth[k],
					                                    t.green_u, t.green_v, material, tl[i].lv);
			}
		}
		t.turf = turf;
		t.shade_grass = 0;
	}
	terra_tris_mottle(&t, mottle);
}

static inline __attribute__((always_inline))
void mesh_face(const TerraTris *t, const uint8_t *points, const uint8_t *q,
              int32_t green_down, TerraTri *tri, TerraTurf *turf)
{
	uint8_t material = q[3], level = mesh_detail_level(material, q[4]);
	uint16_t k;

	tri->base = PAL_TERRAIN + material * PAL_RAMP_N;
	tri->lv = level;
	for (k = 0; k < 3; k++) {
		const uint8_t *p = points + q[k] * MP_SIZE;
		int16_t light;

		tri->x[k] = S16(p, MP_X);
		tri->y[k] = S16(p, MP_Y);
		if (material > SURF_BUNKER && material != SURF_TEE) continue;
		turf->u[k] = S16(p, MP_U);
		turf->v[k] = S16(p, MP_V);
		light = (int8_t)p[MP_LIGHT];
		if (material <= SURF_DEEP_ROUGH) {
			int16_t yards = (int16_t)((int32_t)be32(p + MP_DEPTH) >> 8);
			turf->depth[k] = yards;
			light = mesh_grass_light(turf->u[k], turf->v[k], light, yards,
			                         t->green_u, t->green_v, material, level);
		} else if (material == SURF_GREEN || material == SURF_FRINGE) {
			light = mesh_green_light(green_down, (int32_t)be32(p + MP_DOWN), (int8_t)light);
		} else if (material == SURF_BUNKER) {
			light = (int8_t)p[MP_SAND];
		} else {
			light = (light >> 2) - mesh_depth_span((int32_t)be32(p + MP_DEPTH) >> 8, 18, 32);
		}
		turf->light[k] = light;
	}
}

static void mesh_faces(const uint8_t *b, const int8_t *mottle)
{
	TerraTris t;
	TerraTri tris[TERRA_MESH_TRIS];
	TerraTurf turf[TERRA_MESH_TRIS];
	const uint8_t *points = (const uint8_t *)P32(b, TMF_POINTS);
	const uint8_t *faces = (const uint8_t *)P32(b, TMF_FACES);
	const uint8_t *bins = (const uint8_t *)P32(b, TMF_BINS);
	int32_t green_down = (int32_t)be32(b + TMF_GREEN_DOWN);
	uint16_t i;

	tri_setup(&t, (const uint8_t *)P32(b, TMF_DRAW));
	t.tri = tris;
	t.turf = turf;
	t.shade_grass = 0;
	if (bins) {
		const uint8_t *next = (const uint8_t *)P32(b, TMF_NEXT);

		t.n = 0;
		for (i = MESH_TILE_BINS; i;) {
			uint16_t link = be16(bins + 2 * --i);

			while (link) {
				uint16_t k = link - 1;

				mesh_face(&t, points, faces + 5 * k, green_down, tris + t.n, turf + t.n);
				if (++t.n == TERRA_MESH_TRIS) {
					terra_tris_mottle(&t, mottle);
					t.n = 0;
				}
				link = be16(next + 2 * k);
			}
		}
	} else {
		const uint8_t *order = (const uint8_t *)P32(b, TMF_ORDER);
		uint16_t count = be16(b + TMF_COUNT);

		t.n = count > TERRA_MESH_TRIS ? TERRA_MESH_TRIS : (int16_t)count;
		for (i = 0; i < t.n; i++)
			mesh_face(&t, points, faces + 5 * be16(order + 2 * i), green_down, tris + i, turf + i);
	}
	if (t.n) terra_tris_mottle(&t, mottle);
}

static void sky(uint8_t *b)
{
	TerraSky t;
	int16_t *yb;

	t.bits = (uint8_t *)P32(b, TS_BITS);
	t.bk = (const uint8_t *)P32(b, TS_BK);
	t.row = (uint8_t *)P32(b, TS_ROW);
	yb = (int16_t *)P32(b, TS_YBUF);
	t.bayer = (const uint8_t *)P32(b, TS_BAYER);
	t.pat2 = (const uint8_t *)P32(b, TS_PAT2);
	t.grey = (const uint8_t *)P32(b, TS_GREY);
	t.len = be32(b + TS_LEN);
	t.pos = be32(b + TS_POS);
	t.rb = be16(b + TS_RB);
	t.w = S16(b, TS_W);
	t.y0 = S16(b, TS_Y0);
	t.y1 = S16(b, TS_Y1);
	t.yh = S16(b, TS_YH);
	t.off = S16(b, TS_OFF);
	t.bw = S16(b, TS_BW);
	t.bh = S16(b, TS_BH);
	t.btop = S16(b, TS_BTOP);
	t.lg = b[TS_LG];
	t.px = 0;
	t.ybuf = yb;
	swap16(yb, t.w);
	terra_sky(&t);
	swap16(yb, t.w);
	b[TS_POS] = (uint8_t)(t.pos >> 24);
	b[TS_POS + 1] = (uint8_t)(t.pos >> 16);
	b[TS_POS + 2] = (uint8_t)(t.pos >> 8);
	b[TS_POS + 3] = (uint8_t)t.pos;
	b[TS_BW] = (uint8_t)((uint16_t)t.bw >> 8);
	b[TS_BW + 1] = (uint8_t)t.bw;
}

static void map(const uint8_t *b)
{
	TerraMap t;

	t.bits = (uint8_t *)P32(b, TM_BITS);
	t.cells = (const uint8_t *)P32(b, TM_CELLS);
	t.xcell = (const uint8_t *)P32(b, TM_XCELL);
	t.col = (const uint8_t *)P32(b, TM_COL);
	t.bayer = (const uint8_t *)P32(b, TM_BAYER);
	t.pat2 = (const uint8_t *)P32(b, TM_PAT2);
	t.grey = (const uint8_t *)P32(b, TM_GREY);
	t.len = (fixed)be32(b + TM_LEN);
	t.ypp = (fixed)be32(b + TM_YPP);
	t.rb = be16(b + TM_RB);
	t.w = S16(b, TM_W);
	t.y0 = S16(b, TM_Y0);
	t.y1 = S16(b, TM_Y1);
	t.top = S16(b, TM_TOP);
	t.mh = S16(b, TM_MH);
	t.gs = be16(b + TM_GS);
	t.gh = be16(b + TM_GH);
	t.lg = b[TM_LG];
	terra_map(&t);
}

static void curve_shade(const uint8_t *b)
{
	TerraCurveShade t;

	t.bits = (uint8_t *)P32(b, TC_BITS);
	t.light = (const int8_t *)P32(b, TC_LIGHT);
	t.bayer = (const uint8_t *)P32(b, TC_BAYER);
	t.spans = (const uint8_t *)P32(b, TC_SPANS);
	t.rb = be16(b + TC_RB);
	t.count = be16(b + TC_COUNT);
	t.noise = (const int8_t *)P32(b, TC_NOISE);
	terra_curve_shade(&t);
}

/* Bilinear height in the same 8.8 byte steps as the 68k renderer. */
static __inline__ fixed curve_height(const uint8_t *heights, uint16_t stride, uint16_t step,
                                     uint16_t shift, fixed mx, fixed my, fixed x, fixed y)
{
	fixed gx = x >> shift, gy = y >> shift;
	uint16_t ix, iy, next, fx, fy;
	const uint8_t *top, *bottom;
	int32_t a, b, height;

	if (gx < 0) gx = 0;
	if (gx > mx) gx = mx;
	if (gy < 0) gy = 0;
	if (gy > my) gy = my;
	ix = (uint16_t)(gx >> 16);
	iy = (uint16_t)(gy >> 16);
	next = gx < mx ? ix + 1 : ix;
	fx = (uint16_t)((gx >> 8) & 255);
	fy = (uint16_t)((gy >> 8) & 255);
	top = heights + MULU16(iy, stride);
	bottom = gy < my ? top + stride : top;
	a = ((int32_t)top[ix] << 8) + MULS16(top[next] - top[ix], fx);
	b = ((int32_t)bottom[ix] << 8) + MULS16(bottom[next] - bottom[ix], fx);
	height = a + (MULS16((b - a) >> 1, fy) >> 7);
	/* The signed vertical interpolation can round one step below zero. */
	if (height < 0) return height * (int32_t)step / 16;
	return (fixed)(MULU16(height, step) >> 4);
}

static __inline__ fixed curve_step(fixed delta, uint16_t at)
{
	int64_t product = (int64_t)delta * at;
	return (fixed)(product / 4);
}

static __inline__ fixed curve_mulq(fixed a, int16_t q)
{
	int64_t product = (int64_t)a * q;
	return (fixed)(product / 16384);
}

static void curve_light(const uint8_t *b)
{
	int8_t *out = (int8_t *)P32(b, TCL_OUT);
	const uint8_t *heights = (const uint8_t *)P32(b, TCL_HEIGHTS);
	fixed camx = (fixed)be32(b + TCL_CAMX), camy = (fixed)be32(b + TCL_CAMY);
	fixed base = (fixed)be32(b + TCL_BASE);
	fixed z = (fixed)be32(b + TCL_Z) - base, zref = (fixed)be32(b + TCL_ZREF) - base;
	fixed forward_x = (fixed)be32(b + TCL_DX), forward_y = (fixed)be32(b + TCL_DY);
	fixed dz = (fixed)be32(b + TCL_DZ);
	int16_t f = S16(b, TCL_F), cx = S16(b, TCL_CX);
	int16_t rx = S16(b, TCL_RX), ry = S16(b, TCL_RY), x = 0;
	uint16_t cols = be16(b + TCL_COLS), step = be16(b + TCL_STEP);
	uint16_t hw = be16(b + TCL_HW), hh = be16(b + TCL_HH), shift = be16(b + TCL_SHIFT);
	fixed mx = (fixed)(hw - 1) << 16, my = (fixed)(hh - 1) << 16;

	for (; cols; cols--, out++, x += 1 << CURVE_LIGHT_SHIFT) {
		fixed sx = terra_div((int32_t)(x - cx) * FIX_ONE, f);
		fixed dx = forward_x + curve_mulq(sx, rx), dy = forward_y + curve_mulq(sx, ry);
		fixed wx, wy, rise, level;
		uint16_t lo = 4, hi = 360;
		int16_t light;

		while (hi - lo > 1) {
			uint16_t mid = (uint16_t)((lo + hi) / 2);
			wx = camx + curve_step(dx, mid);
			wy = camy + curve_step(dy, mid);
			if (z + curve_step(dz, mid) > curve_height(heights, hw, step, shift, mx, my, wx, wy))
				lo = mid;
			else hi = mid;
		}
		wx = camx + curve_step(dx, hi);
		wy = camy + curve_step(dy, hi);
		rise = (curve_height(heights, hw, step, shift, mx, my, wx + 2 * FIX_ONE, wy) -
		        curve_height(heights, hw, step, shift, mx, my, wx - 2 * FIX_ONE, wy)) * 3 / 4;
		rise += (curve_height(heights, hw, step, shift, mx, my, wx, wy + 2 * FIX_ONE) -
		         curve_height(heights, hw, step, shift, mx, my, wx, wy - 2 * FIX_ONE)) / 2;
		level = curve_height(heights, hw, step, shift, mx, my, wx, wy) - zref;
		light = (int16_t)((level * 2 - rise * 12) >> FIX_SHIFT);
		*out = (int8_t)(light < -20 ? -20 : light > 20 ? 20 : light);
	}
}

static __inline__ void put16(uint8_t *b, uint16_t n)
{
	b[0] = (uint8_t)(n >> 8);
	b[1] = (uint8_t)n;
}

static __inline__ void put32(uint8_t *b, uint32_t n)
{
	put16(b, (uint16_t)(n >> 16));
	put16(b + 2, (uint16_t)n);
}

static __inline__ int16_t mesh_pixel(int32_t position, int32_t depth, int16_t f, int16_t centre)
{
	int32_t p = centre + terra_div(position * f, depth);
	return (int16_t)(p < -MESH_TILE_COORD ? -MESH_TILE_COORD : p > MESH_TILE_COORD ? MESH_TILE_COORD : p);
}

/* Points and links share the 68k byte order. Walk the faces backwards
 * so each bin retains the course's original order for equal depths. */
static uint8_t mesh_bins(const uint8_t *q, uint16_t n, const uint8_t *points,
                      const uint8_t *outside, uint8_t *next, uint8_t *bins,
                      int32_t low, int32_t high)
{
	uint32_t span = 3 * (uint32_t)(high - low);
	int32_t base = 3 * low;
	uint8_t shift = 0, has_bunker = 0;
	uint16_t i;

	while ((span >> shift) >= MESH_TILE_BINS) shift++;
	for (i = 0; i < MESH_TILE_BINS; i++) put16(bins + i * 2, 0);
	for (q += n * 5; n;) {
		const uint8_t *a, *b, *c;
		uint8_t oa, ob, oc;
		uint16_t bin;
		q -= 5;
		n--;
		oa = outside[q[0]];
		ob = outside[q[1]];
		oc = outside[q[2]];
		if (oa & ob & oc) continue;
		a = points + q[0] * MP_SIZE;
		b = points + q[1] * MP_SIZE;
		c = points + q[2] * MP_SIZE;
		if (!((oa | ob | oc) & MESH_OUT_NEAR) &&
		    be16(a + MP_Y) == be16(b + MP_Y) && be16(b + MP_Y) == be16(c + MP_Y)) continue;
		bin = (uint16_t)(((uint32_t)((int32_t)be32(a + MP_DEPTH) +
		      (int32_t)be32(b + MP_DEPTH) + (int32_t)be32(c + MP_DEPTH) - base)) >> shift);
		if (q[3] == SURF_BUNKER) has_bunker = 1;
		put16(next + n * 2, be16(bins + bin * 2));
		put16(bins + bin * 2, n + 1);
	}
	return has_bunker;
}

static void mesh_project(uint8_t *b)
{
	const uint8_t *v = (const uint8_t *)P32(b, TMP_VERTICES);
	uint8_t *p = (uint8_t *)P32(b, TMP_POINTS);
	uint8_t *outside = (uint8_t *)P32(b, TMP_OUTSIDE);
	const int8_t *light = (const int8_t *)P32(b, TMP_LIGHT);
	int32_t side0 = (int32_t)be32(b + TMP_ORIGIN_SIDE);
	int32_t depth0 = (int32_t)be32(b + TMP_ORIGIN_DEPTH);
	int32_t height = (int32_t)be32(b + TMP_HEIGHT), low = INT32_MAX, high = INT32_MIN;
	int16_t fx = S16(b, TMP_FX), fy = S16(b, TMP_FY), rx = S16(b, TMP_RX), ry = S16(b, TMP_RY);
	int16_t cx = S16(b, TMP_CX), yh = S16(b, TMP_YH), f = S16(b, TMP_F);
	int16_t w = S16(b, TMP_W), vh = S16(b, TMP_VH);
	uint16_t n = be16(b + TMP_COUNT), i;
	uint8_t projected = 0;

	for (i = 0; i < n; i++, v += 6, p += MP_SIZE) {
		int16_t u = S16(v, 0), v0 = S16(v, 2);
		int32_t depth = (u * fx + v0 * fy - depth0) >> 10;
		int32_t side = (u * rx + v0 * ry - side0) >> 10;
		int32_t down = height - S16(v, 4);

		if (depth < low) low = depth;
		if (depth > high) high = depth;
		put32(p + MP_SIDE, (uint32_t)side);
		put32(p + MP_DOWN, (uint32_t)down);
		put32(p + MP_DEPTH, (uint32_t)depth);
		if (light) {
			put16(p + MP_U, (uint16_t)u);
			put16(p + MP_V, (uint16_t)v0);
			p[MP_LIGHT] = (uint8_t)mesh_light_sample(light, b[TMP_LIGHT_W], b[TMP_LIGHT_H],
			                                       b[TMP_LIGHT_SHIFT], u, v0);
			p[MP_SAND] = MESH_SAND_UNLIT;
		}
		if (depth < MESH_TILE_NEAR) outside[i] = MESH_OUT_NEAR;
		else {
			int16_t x = mesh_pixel(side, depth, f, cx), y = mesh_pixel(down, depth, f, yh);
			put16(p + MP_X, (uint16_t)x);
			put16(p + MP_Y, (uint16_t)y);
			outside[i] = (x < 0 ? MESH_OUT_LEFT : x >= w ? MESH_OUT_RIGHT : 0) |
			             (y < 0 ? MESH_OUT_TOP : y >= vh ? MESH_OUT_BOTTOM : 0);
			projected++;
		}
	}
	put32(b + TMP_LOW, (uint32_t)low);
	put32(b + TMP_HIGH, (uint32_t)high);
	b[TMP_PROJECTED] = projected;
	b[TMP_HAS_BUNKER] = mesh_bins(v, be16(b + TMP_TRIANGLES), (const uint8_t *)P32(b, TMP_POINTS),
	          outside, (uint8_t *)P32(b, TMP_NEXT), (uint8_t *)P32(b, TMP_BINS), low, high);
}

static void edges(const uint8_t *b)
{
	TerraEdges t;
	t.bits = (uint8_t *)P32(b, 0);
	t.marks = (uint8_t *)P32(b, 4);
	t.rb = be16(b + 8);
	t.w = be16(b + 10);
	t.vh = be16(b + 12);
	t.horizon = S16(b, 14);
	t.y = be16(b + 16);
	t.end = be16(b + 18);
	terra_edges(&t);
}

static void object(uint8_t *b)
{
	uint8_t *bits = (uint8_t *)P32(b, TOBJ_BITS);
	const uint8_t *src = (const uint8_t *)P32(b, TOBJ_SRC);
	const uint8_t *clip = (const uint8_t *)P32(b, TOBJ_CLIP);
	uint32_t us = be32(b + TOBJ_US), vs = be32(b + TOBJ_VS), pixels = 0;
	uint16_t rb = be16(b + TOBJ_RB), sw = be16(b + TOBJ_SW), sh = be16(b + TOBJ_SH);
	int16_t x0 = S16(b, TOBJ_X0), y0 = S16(b, TOBJ_Y0), vh = S16(b, TOBJ_VH);
	int16_t ys = y0 < 0 ? 0 : y0, bottom = y0 + S16(b, TOBJ_DH);
	int16_t x, end = S16(b, TOBJ_C1);

	if (bottom > vh) bottom = vh;
	if (src) {
		uint32_t start_v = (uint32_t)(ys - y0) * vs;
		for (x = S16(b, TOBJ_C0); x < end; x++, clip += 2) {
			int16_t ye = S16(clip, 0), y;
			uint16_t u = (uint16_t)(((uint32_t)(x - x0) * us) >> 16);
			uint32_t v = start_v;
			const uint8_t *col;
			uint8_t *p;

			if (ye > bottom) ye = bottom;
			if (ys >= ye) continue;
			if (u >= sw) u = sw - 1;
			col = src + u;
			p = bits + MULU16(ys, rb) + x;
			for (y = ys; y < ye; y++, p += rb, v += vs) {
				uint16_t sv = (uint16_t)(v >> 16);
				uint8_t c;
				if (sv >= sh) break;
				c = col[MULU16(sv, sw)];
				if (c) *p = c;
			}
			pixels += ye - ys;
		}
	} else {
		int16_t scale = S16(b, TOBJ_SCALE), flag_end = y0 + (int16_t)sh;
		if (flag_end > vh) flag_end = vh;
		for (x = S16(b, TOBJ_C0); x < end; x++, clip += 2) {
			int pole = x - x0 < (int16_t)sw;
			int16_t edge = pole ? bottom : flag_end, ye = S16(clip, 0), y;
			uint8_t *p;

			if (ye > edge) ye = edge;
			if (ys >= ye) continue;
			p = bits + MULU16(ys, rb) + x;
			for (y = ys; y < ye; y++, p += rb)
				*p = pole ? PAL_GREY + 7 : PAL_FLAG + 2 - (y - y0 >= (int16_t)sh - scale);
		}
	}
	put32(b + TOBJ_PIXELS, pixels);
}

unsigned long TO16ARM_ENTRY terraarm_entry(const void *emul, void *user, void *call68k)
{
	const uint8_t *u = (const uint8_t *)user;
	uint8_t *b = (uint8_t *)P32(u, 2);
	uint16_t op = be16(u);

	(void)emul;
	(void)call68k;
	if (op == TERRA_ROW) row(b);
	else if (op == TERRA_TRIS) tris(b, (const int8_t *)P32(u, TERRA_ARG_MOTTLE));
	else if (op == TERRA_MESH_FACES) mesh_faces(b, (const int8_t *)P32(u, TERRA_ARG_MOTTLE));
	else if (op == TERRA_SKY) sky(b);
	else if (op == TERRA_MAP) map(b);
	else if (op == TERRA_CURVE) curve_shade(b);
	else if (op == TERRA_MESH_PROJECT) mesh_project(b);
	else if (op == TERRA_EDGES) edges(b);
	else if (op == TERRA_CURVE_LIGHT) curve_light(b);
	else if (op == TERRA_OBJECT) object(b);
	else if (op == TERRA_MESH_LIGHT) {
		TerraMeshLight t = {(const uint8_t *)P32(b, TML_HEIGHTS), (int8_t *)P32(b, TML_OUT),
		                    (int32_t)be32(b + TML_GAIN), be16(b + TML_WIDTH), be16(b + TML_HEIGHT),
		                    be16(b + TML_FIRST), be16(b + TML_END)};
		terra_mesh_light(&t);
	} else if (op == TERRA_MESH_CHECK) {
		TerraMeshCheck t;

		t.data = (const uint8_t *)P32(b, TMV_DATA);
		t.size = be32(b + TMV_SIZE);
		t.width = be16(b + TMV_WIDTH);
		t.length = be16(b + TMV_LENGTH);
		terra_mesh_check(&t);
		put16(b + TMV_TILES, t.tiles);
		put16(b + TMV_VERTICES, t.vertices);
		put16(b + TMV_TRIANGLES, t.triangles);
		put16(b + TMV_MOW_PHASE, t.mow_phase);
	}
	return 0;
}
