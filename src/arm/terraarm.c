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

static void tris(const uint8_t *b)
{
	TerraTris t;
	TerraTri tl[TERRA_TRIS_MAX];
	TerraTurf turf[TERRA_TRIS_MAX];
	const uint8_t *q;
	int16_t i, k;

	t.bits = (uint8_t *)P32(b, TT_BITS);
	q = (const uint8_t *)P32(b, TT_TRI);
	t.bayer = (const uint8_t *)P32(b, TT_BAYER);
	t.pat2 = (const uint8_t *)P32(b, TT_PAT2);
	t.grey = (const uint8_t *)P32(b, TT_GREY);
	t.rb = be16(b + TT_RB);
	t.w = S16(b, TT_W);
	t.vh = S16(b, TT_VH);
	t.n = S16(b, TT_N);
	t.lg = b[TT_LG];
	t.noise = (const int8_t *)P32(b, TT_NOISE);
	t.mow_phase = S16(b, TT_MOW_PHASE);
	t.reflection = (const uint8_t *)P32(b, TT_REFLECTION);
	t.turf = 0;
	if (t.n < 0) t.n = 0;
	if (t.n > TERRA_TRIS_MAX) t.n = TERRA_TRIS_MAX;
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
			for (k = 0; k < 3; k++) {
				turf[i].u[k] = S16(q, 2 * k);
				turf[i].v[k] = S16(q, 6 + 2 * k);
				turf[i].light[k] = S16(q, 12 + 2 * k);
				turf[i].depth[k] = S16(q, 18 + 2 * k);
			}
		}
		t.turf = turf;
	}
	terra_tris(&t);
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

unsigned long TO16ARM_ENTRY terraarm_entry(const void *emul, void *user, void *call68k)
{
	const uint8_t *u = (const uint8_t *)user;
	uint8_t *b = (uint8_t *)P32(u, 2);
	uint16_t op = be16(u);

	(void)emul;
	(void)call68k;
	if (op == TERRA_ROW) row(b);
	else if (op == TERRA_TRIS) tris(b);
	else if (op == TERRA_SKY) sky(b);
	else if (op == TERRA_MAP) map(b);
	else if (op == TERRA_CURVE) curve_shade(b);
	else if (op == TERRA_MESH_PROJECT) mesh_project(b);
	else if (op == TERRA_EDGES) edges(b);
	return 0;
}
