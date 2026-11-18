/* Course loading tests. Run by `make course-test`:
 *   coursetest COURSE.pdb ROUNDTRIP.pdb OUT.ppm
 * The second PDB is the first dumped to text and compiled again; both must
 * hold the same records. OUT.ppm gets the test hole's surface grid. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hole.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

typedef struct {
	int count;
	uint8_t *rec[40];
	uint32_t len[40];
	char name[33];
} Pdb;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/* Each record gets its own exact-size copy so ASan sees any read past its end. */
static int load_pdb(const char *path, Pdb *db)
{
	FILE *f = fopen(path, "rb");
	uint8_t *d;
	long n;
	int i;

	if (!f) return 0;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	d = malloc(n);
	if (fread(d, 1, n, f) != (size_t)n) n = 0;
	fclose(f);
	if (n < 78 || memcmp(d + 60, "CrseP386", 8) != 0) { free(d); return 0; }
	memcpy(db->name, d, 32);
	db->name[32] = 0;
	db->count = d[76] << 8 | d[77];
	if (db->count > 40) db->count = 40;
	for (i = 0; i < db->count; i++) {
		uint32_t a = be32(d + 78 + 8 * i);
		uint32_t b = i + 1 < db->count ? be32(d + 78 + 8 * (i + 1)) : (uint32_t)n;
		db->len[i] = b - a;
		db->rec[i] = malloc(b - a ? b - a : 1);
		memcpy(db->rec[i], d + a, b - a);
	}
	free(d);
	return 1;
}

static uint8_t grid[128 * 640];
static int rows_seen, rows_bad;

static void put_row(void *ctx, uint16_t row, const uint8_t *bytes, uint16_t len)
{
	SurfaceGrid *g = ctx;
	if (row != rows_seen || len != g->stride) rows_bad++;
	memcpy(grid + (uint32_t)row * g->stride, bytes, len);
	rows_seen++;
}

static fixed Y(double v) { return (fixed)lround(v * 65536.0); }

static uint8_t at(const SurfaceGrid *g, double x, double y) { return surface_at(g, Y(x), Y(y)); }

/* The height lookup done in doubles from the raw bytes. */
static double ref_height(const Hole *h, double x, double y)
{
	double cell = 1 << h->hm_shift, gx = x / cell, gy = y / cell, fx, fy, t, b;
	int i, j, i1, j1;
	if (gx < 0) gx = 0;
	if (gy < 0) gy = 0;
	if (gx > h->hm_w - 1) gx = h->hm_w - 1;
	if (gy > h->hm_h - 1) gy = h->hm_h - 1;
	i = (int)gx; j = (int)gy;
	fx = gx - i; fy = gy - j;
	i1 = i + 1 < h->hm_w ? i + 1 : i;
	j1 = j + 1 < h->hm_h ? j + 1 : j;
	t = h->heights[j * h->hm_w + i] * (1 - fx) + h->heights[j * h->hm_w + i1] * fx;
	b = h->heights[j1 * h->hm_w + i] * (1 - fx) + h->heights[j1 * h->hm_w + i1] * fx;
	return h->base / 65536.0 + (t * (1 - fy) + b * fy) * h->step / 65536.0;
}

static void write_ppm(const char *path, const Hole *h, const SurfaceGrid *g)
{
	static const uint8_t pal[SURF_COUNT][3] = {
		{ 40, 110, 40 }, { 70, 150, 60 }, { 100, 185, 80 }, { 25, 75, 30 },
		{ 140, 220, 110 }, { 115, 200, 95 }, { 225, 210, 150 }, { 50, 90, 200 },
		{ 160, 150, 140 }, { 230, 230, 230 }, { 120, 205, 100 }
	};
	const int S = 3;
	FILE *f = fopen(path, "wb");
	int x, y, w = g->w * S, hh = g->h * S;
	uint16_t i;
	uint8_t *img;

	if (!f) { printf("can't write %s\n", path); fails++; return; }
	img = malloc((size_t)w * hh * 3);
	/* Far end at the top, like a course map. */
	for (y = 0; y < hh; y++)
		for (x = 0; x < w; x++) {
			int cy = g->h - 1 - y / S, cx = x / S;
			uint8_t b = g->cells[cy * g->stride + (cx >> 1)];
			uint8_t t = (cx & 1) ? (b & 15) : (b >> 4);
			memcpy(img + ((size_t)y * w + x) * 3, pal[t], 3);
		}
	/* Objects as dark dots, tees blue, pins red. */
	for (i = 0; i < h->object_count + h->tee_count + HOLE_PINS; i++) {
		vec2 p;
		uint8_t c[3] = { 10, 40, 10 };
		int dx, dy;
		if (i < h->object_count) {
			HoleObject o;
			hole_object(h, i, &o);
			p = o.pos;
		} else if (i < h->object_count + h->tee_count) {
			p = h->tees[i - h->object_count].pos;
			c[0] = 0; c[1] = 0; c[2] = 255;
		} else {
			p = h->pins[i - h->object_count - h->tee_count];
			c[0] = 255; c[1] = 0; c[2] = 0;
		}
		for (dy = -1; dy <= 1; dy++)
			for (dx = -1; dx <= 1; dx++) {
				int px = (int)(p.x * S / 2 / 65536) + dx;
				int py = hh - 1 - (int)(p.y * S / 2 / 65536) + dy;
				if (px >= 0 && px < w && py >= 0 && py < hh) memcpy(img + ((size_t)py * w + px) * 3, c, 3);
			}
	}
	fprintf(f, "P6\n%d %d\n255\n", w, hh);
	fwrite(img, 1, (size_t)w * hh * 3, f);
	fclose(f);
	free(img);
}

static uint32_t seed = 12345;
static uint32_t rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

/* Parses buf and, if it's accepted, uses everything that reads the record. */
static int try_hole(const uint8_t *buf, uint32_t len)
{
	Hole h;
	int r = hole_parse(&h, buf, len);
	if (r == HOLE_OK) {
		static uint8_t row[SURF_ROW_MAX];
		SurfaceGrid g;
		uint16_t i;
		HoleObject o;
		surface_size(&h, &g);
		rows_seen = 0;
		surface_raster(&h, row, put_row, &g);
		hole_height(&h, Y(50), Y(50));
		hole_height(&h, Y(1e4), Y(-1e4));
		for (i = 0; i < h.object_count; i++) hole_object(&h, i, &o);
	}
	return r;
}

static void fuzz(const uint8_t *rec, uint32_t len, int is_hole)
{
	uint8_t *b;
	uint32_t n, i;
	int k, rejected = 1;
	Course c;

	/* Every shorter length is rejected, read from an exact-size buffer. */
	for (n = 0; n < len; n++) {
		b = malloc(n ? n : 1);
		memcpy(b, rec, n);
		if ((is_hole ? try_hole(b, n) : course_parse(&c, b, n)) == HOLE_OK) rejected = 0;
		free(b);
	}
	CHECK(rejected);

	/* One byte more is rejected. */
	b = malloc(len + 1);
	memcpy(b, rec, len);
	b[len] = 0;
	CHECK((is_hole ? try_hole(b, len + 1) : course_parse(&c, b, len + 1)) == HOLE_ERR_LONG);
	free(b);

	/* Random damage must never read out of bounds or crash. */
	b = malloc(len);
	for (k = 0; k < 20000; k++) {
		int hits = 1 + rnd() % 4;
		memcpy(b, rec, len);
		while (hits--) b[rnd() % len] = (uint8_t)rnd();
		if (is_hole) try_hole(b, len);
		else course_parse(&c, b, len);
	}
	/* Every byte set to 0 and 255 in turn. */
	for (i = 0; i < len; i++) {
		memcpy(b, rec, len);
		b[i] = 0;
		if (is_hole) try_hole(b, len); else course_parse(&c, b, len);
		b[i] = 255;
		if (is_hole) try_hole(b, len); else course_parse(&c, b, len);
	}
	free(b);
}

int main(int argc, char **argv)
{
	Pdb a, b;
	Course c;
	Hole h;
	HoleObject o;
	SurfaceGrid g;
	static uint8_t row[SURF_ROW_MAX];
	uint8_t *bad;
	int i;
	double worst = 0;

	if (argc != 4) {
		printf("usage: coursetest COURSE.pdb ROUNDTRIP.pdb OUT.ppm\n");
		return 2;
	}
	if (!load_pdb(argv[1], &a) || !load_pdb(argv[2], &b)) {
		printf("FAIL can't read the PDBs\n");
		return 1;
	}
	CHECK(strcmp(a.name, "Par386 Test") == 0);
	CHECK(a.count == 4);

	/* Round trip through the dump. */
	CHECK(a.count == b.count);
	for (i = 0; i < a.count && i < b.count; i++)
		CHECK(a.len[i] == b.len[i] && memcmp(a.rec[i], b.rec[i], a.len[i]) == 0);

	/* Course header. */
	CHECK(course_parse(&c, a.rec[0], a.len[0]) == HOLE_OK);
	CHECK(strcmp(c.name, "Test") == 0);
	CHECK(c.hole_count == 3 && c.tee_count == 3);
	CHECK(strcmp(c.tee_names[0], "Blue") == 0 && strcmp(c.tee_names[2], "Red") == 0);
	CHECK(c.par[0] == 4 && c.yards[0] == 413);
	CHECK(c.par[1] == 3 && c.par[2] == 5 && c.yards[2] > 470);

	/* Hole fields. */
	CHECK(hole_parse(&h, a.rec[1], a.len[1]) == HOLE_OK);
	CHECK(h.number == 1 && h.par == 4);
	CHECK(h.width == 200 && h.length == 440);
	CHECK(h.hm_w == 32 && h.hm_h == 64 && h.hm_shift == 3);
	CHECK(h.base == 0 && h.step == FIX_ONE / 16);
	CHECK(h.base_surface == SURF_ROUGH);
	CHECK(strcmp(h.name, "Elbow Pond") == 0);
	CHECK(h.aim.x == Y(62) && h.aim.y == Y(240));
	CHECK(h.tee_count == 3);
	CHECK(h.tees[0].set == 0 && h.tees[0].pos.x == Y(60) && h.tees[0].pos.y == Y(12) && h.tees[0].yards == 413);
	CHECK(h.tees[2].set == 2 && h.tees[2].pos.y == Y(40) && h.tees[2].yards == 385);
	CHECK(h.pins[0].x == Y(140) && h.pins[0].y == Y(405));
	CHECK(h.pins[3].x == Y(136) && h.pins[3].y == Y(416));
	CHECK(h.shape_count == 13);
	CHECK(h.object_count == 18);
	hole_object(&h, 0, &o);
	CHECK(o.type == OBJ_PINE && o.variant == 0 && o.scale == 72 && o.pos.x == Y(16) && o.pos.y == Y(60));
	hole_object(&h, 17, &o);
	CHECK(o.type == OBJ_ROCK && o.scale == 48 && o.pos.x == Y(160) && o.pos.y == Y(252));
	printf("hole record %u bytes, course header %u bytes\n", (unsigned)a.len[1], (unsigned)a.len[0]);

	/* Surface grid. */
	surface_size(&h, &g);
	CHECK(g.w == 100 && g.h == 220 && g.stride == 50);
	rows_seen = rows_bad = 0;
	surface_raster(&h, row, put_row, &g);
	CHECK(rows_seen == 220 && rows_bad == 0);
	g.cells = grid;
	CHECK(at(&g, 60, 12) == SURF_TEE);
	CHECK(at(&g, 62, 40) == SURF_TEE);
	for (i = 0; i < HOLE_PINS; i++)
		CHECK(surface_at(&g, h.pins[i].x, h.pins[i].y) == SURF_GREEN);
	CHECK(at(&g, 140, 384.5) == SURF_FRINGE);
	CHECK(at(&g, 135, 235) == SURF_WATER);
	CHECK(at(&g, 150, 240) == SURF_WATER);
	CHECK(at(&g, 64, 150) == SURF_FAIRWAY);
	CHECK(at(&g, 130, 300) == SURF_FAIRWAY);
	CHECK(at(&g, 89, 150) == SURF_FIRST_CUT);
	CHECK(at(&g, 38, 236) == SURF_BUNKER);
	CHECK(at(&g, 158, 420) == SURF_BUNKER);
	CHECK(at(&g, 27.5, 200) == SURF_PATH);
	CHECK(at(&g, 17, 200) == SURF_DEEP_ROUGH);
	CHECK(at(&g, 5, 200) == SURF_OOB);
	CHECK(at(&g, 185, 100) == SURF_ROUGH);
	CHECK(at(&g, -1, 100) == SURF_OOB);
	CHECK(at(&g, 100, 441) == SURF_OOB);
	CHECK(at(&g, 201, 100) == SURF_OOB);
	write_ppm(argv[3], &h, &g);

	/* Heights: exact on the points, bilinear between, level past the edge. */
	CHECK(hole_height(&h, Y(40), Y(80)) == (fixed)h.heights[10 * 32 + 5] * h.step);
	for (i = 0; i < 2000; i++) {
		double x = (rnd() % 26000) / 100.0 - 5, y = (rnd() % 52000) / 100.0 - 5;
		double e = fabs(hole_height(&h, Y(x), Y(y)) / 65536.0 - ref_height(&h, x, y));
		if (e > worst) worst = e;
	}
	/* Fractions are cut to 1/256 of a cell, so up to 1/256 of the largest step between points. */
	CHECK(worst < 0.01);
	printf("height lookup worst error %.5f yards\n", worst);
	CHECK(hole_height(&h, Y(-50), Y(-50)) == hole_height(&h, 0, 0));
	CHECK(hole_height(&h, Y(140), Y(422)) > hole_height(&h, Y(140), Y(390)) + Y(0.5));
	CHECK(hole_height(&h, Y(135), Y(235)) < hole_height(&h, Y(64), Y(235)));

	/* Bad records. */
	bad = malloc(a.len[1]);
	memcpy(bad, a.rec[1], a.len[1]);
	bad[1] = 2;    /* version */
	CHECK(hole_parse(&h, bad, a.len[1]) == HOLE_ERR_VERSION);
	memcpy(bad, a.rec[1], a.len[1]);
	bad[3] = 9;    /* par */
	CHECK(hole_parse(&h, bad, a.len[1]) == HOLE_ERR_VALUE);
	memcpy(bad, a.rec[1], a.len[1]);
	bad[10] = 2;   /* 4 yard height cells no longer cover the hole */
	CHECK(hole_parse(&h, bad, a.len[1]) == HOLE_ERR_VALUE);
	memcpy(bad, a.rec[1], a.len[1]);
	bad[16] = 0xFF;    /* aim off the hole */
	CHECK(hole_parse(&h, bad, a.len[1]) == HOLE_ERR_VALUE);
	free(bad);
	/* Holes 2 and 3, made from hole 1 with "from": a par 3 over the pond from
	 * its own tee, and a par 5 on past the old green, now fairway. */
	CHECK(hole_parse(&h, a.rec[2], a.len[2]) == HOLE_OK);
	CHECK(h.number == 2 && h.par == 3 && strcmp(h.name, "Pond Carry") == 0);
	CHECK(h.tee_count == 3 && h.tees[0].pos.x == Y(150) && h.tees[0].pos.y == Y(207));
	CHECK(h.tees[0].yards > 185 && h.tees[0].yards < 205);
	CHECK(h.aim.x == Y(140) && h.aim.y == Y(405) && h.shape_count == 14 && h.object_count == 18);
	surface_size(&h, &g);
	rows_seen = rows_bad = 0;
	surface_raster(&h, row, put_row, &g);
	g.cells = grid;
	CHECK(rows_bad == 0);
	CHECK(at(&g, 150, 207) == SURF_TEE && at(&g, 140, 405) == SURF_GREEN && at(&g, 135, 235) == SURF_WATER);
	CHECK(hole_parse(&h, a.rec[3], a.len[3]) == HOLE_OK);
	CHECK(h.number == 3 && h.par == 5 && h.length == 500 && h.tees[0].pos.y == Y(12));
	CHECK(h.object_count == 19);
	surface_size(&h, &g);
	CHECK(g.h == 250);
	rows_seen = rows_bad = 0;
	surface_raster(&h, row, put_row, &g);
	g.cells = grid;
	CHECK(rows_bad == 0 && rows_seen == 250);
	for (i = 0; i < HOLE_PINS; i++)
		CHECK(surface_at(&g, h.pins[i].x, h.pins[i].y) == SURF_GREEN);
	CHECK(at(&g, 140, 405) == SURF_FAIRWAY && at(&g, 145, 445) == SURF_FAIRWAY);
	CHECK(at(&g, 158, 420) == SURF_BUNKER && at(&g, 5, 470) == SURF_OOB);

	fuzz(a.rec[0], a.len[0], 0);
	fuzz(a.rec[1], a.len[1], 1);

	for (i = 0; i < a.count; i++) free(a.rec[i]);
	for (i = 0; i < b.count; i++) free(b.rec[i]);
	printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
