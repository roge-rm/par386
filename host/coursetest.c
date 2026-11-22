/* Course loading tests. Run by `make course-test`:
 *   coursetest COURSE.pdb ROUNDTRIP.pdb OUT.ppm
 * The second PDB is the first dumped to text and compiled again; both must
 * hold the same records. OUT.ppm gets the test hole's surface grid.
 *   coursetest --course COURSE.pdb ROUNDTRIP.pdb
 * checks any course: the round trip, every hole loads and draws its surface
 * grid, pins are on the green and tees on a tee, the header matches. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define MAX_PDB_RECORDS (3 * COURSE_MAX_HOLES + 2)

typedef struct {
	int count;
	uint8_t *rec[MAX_PDB_RECORDS];
	uint32_t len[MAX_PDB_RECORDS];
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
	if (db->count < 1 || db->count > MAX_PDB_RECORDS || 78 + db->count * 8 > n) {
		free(d);
		return 0;
	}
	for (i = 0; i < db->count; i++) {
		uint32_t a = be32(d + 78 + 8 * i);
		uint32_t b = i + 1 < db->count ? be32(d + 78 + 8 * (i + 1)) : (uint32_t)n;
		if (a < 78 + 8U * db->count || b < a || b > (uint32_t)n) {
			while (i) free(db->rec[--i]);
			free(d);
			return 0;
		}
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

/* Writes a course header of version ver (1 to 4) from c; returns its length. */
static uint32_t header(const Course *c, int ver, uint8_t *b)
{
	uint32_t n = 0;
	int i, k;
	b[n++] = 0;
	b[n++] = (uint8_t)ver;
	b[n++] = c->hole_count;
	b[n++] = c->tee_count;
	b[n++] = (uint8_t)strlen(c->name);
	for (k = 0; c->name[k]; k++) b[n++] = (uint8_t)c->name[k];
	for (i = 0; i < c->tee_count; i++) {
		b[n++] = (uint8_t)strlen(c->tee_names[i]);
		for (k = 0; c->tee_names[i][k]; k++) b[n++] = (uint8_t)c->tee_names[i][k];
	}
	for (i = 0; i < c->hole_count; i++) {
		b[n++] = c->par[i];
		b[n++] = (uint8_t)(c->yards[i] >> 8);
		b[n++] = (uint8_t)c->yards[i];
	}
	if (ver >= 2) {
		b[n++] = c->scenery;
		for (i = 0; i < c->hole_count; i++) b[n++] = c->turn[i];
	}
	if (ver >= 3) b[n++] = c->wind;
	if (ver >= 4) b[n++] = c->green;
	return n;
}

/* Headers of every version: each loads with its own fields and the
 * defaults for the ones it hasn't, and goes through the fuzz. */
static void header_versions(const uint8_t *rec, uint32_t len)
{
	Course c, d;
	uint8_t b[512];
	uint32_t n;
	int v;

	CHECK(course_parse(&c, rec, len) == HOLE_OK);
	c.scenery = 4;
	c.turn[0] = 12;
	c.turn[2] = 200;
	c.wind = 15;
	c.green = 13;
	for (v = 1; v <= 4; v++) {
		n = header(&c, v, b);
		memset(&d, 0x55, sizeof d);
		CHECK(course_parse(&d, b, n) == HOLE_OK);
		CHECK(strcmp(d.name, c.name) == 0 && d.hole_count == c.hole_count && d.tee_count == c.tee_count);
		CHECK(d.scenery == (v >= 2 ? 4 : 0) && d.turn[2] == (v >= 2 ? 200 : 0) && d.turn[COURSE_MAX_HOLES - 1] == 0);
		CHECK(d.wind == (v >= 3 ? 15 : 0) && d.green == (v >= 4 ? 13 : 0));
		fuzz(b, n, 0);
	}
	n = header(&c, 3, b);
	b[n - 1] = 41;
	CHECK(course_parse(&d, b, n) == HOLE_ERR_VALUE);
	n = header(&c, 4, b);
	b[n - 1] = 0;
	CHECK(course_parse(&d, b, n) == HOLE_OK && d.green == 0);
	b[n - 1] = 6;
	CHECK(course_parse(&d, b, n) == HOLE_OK && d.green == 6);
	b[n - 1] = 14;
	CHECK(course_parse(&d, b, n) == HOLE_OK);
	b[n - 1] = 5;
	CHECK(course_parse(&d, b, n) == HOLE_ERR_VALUE);
	b[n - 1] = 15;
	CHECK(course_parse(&d, b, n) == HOLE_ERR_VALUE);
	b[1] = 5;
	CHECK(course_parse(&d, b, n) == HOLE_ERR_VERSION);
}

/* Checks every hole of any course. */
static int terrain_record(const Pdb *db, const Course *c, Hole *h, int k)
{
	int dir = c->hole_count + 1;
	uint16_t detailed, compact;

	if (db->count <= dir) return 0;
	detailed = course_terrain_record(db->rec[dir], db->len[dir], c->hole_count, db->count, k, 0);
	compact = course_terrain_record(db->rec[dir], db->len[dir], c->hole_count, db->count, k, 1);
	if (!detailed) return 0;
	CHECK(terrain_mesh_parse(&h->terrain_mesh, h, db->rec[detailed], db->len[detailed]));
	if (compact == detailed) return 1;
	CHECK(compact && terrain_mesh_parse(&h->terrain_mesh, h, db->rec[compact], db->len[compact]));
	return 2;
}

static int any_course(const char *pa, const char *pb)
{
	Pdb a, b;
	Course c;
	Hole h;
	SurfaceGrid g;
	static uint8_t row[SURF_ROW_MAX];
	uint32_t total = 0;
	int i, k, meshes = 0;

	if (!load_pdb(pa, &a) || !load_pdb(pb, &b)) {
		printf("FAIL can't read the PDBs\n");
		return 1;
	}
	CHECK(a.count == b.count);
	for (i = 0; i < a.count && i < b.count; i++)
		CHECK(a.len[i] == b.len[i] && memcmp(a.rec[i], b.rec[i], a.len[i]) == 0);
	CHECK(course_parse(&c, a.rec[0], a.len[0]) == HOLE_OK);
	CHECK(a.count >= c.hole_count + 1);
	for (k = 1; k <= c.hole_count && k < a.count; k++) {
		total += a.len[k];
		CHECK(a.len[k] < 16384);
		if (hole_parse(&h, a.rec[k], a.len[k]) != HOLE_OK) { CHECK(!"hole parses"); continue; }
		meshes += terrain_record(&a, &c, &h, k);
		CHECK(h.number == k && h.par == c.par[k - 1] && h.tees[0].yards == c.yards[k - 1]);
		surface_size(&h, &g);
		rows_seen = rows_bad = 0;
		surface_raster(&h, row, put_row, &g);
		g.cells = grid;
		CHECK(rows_bad == 0 && rows_seen == g.h);
		for (i = 0; i < HOLE_PINS; i++)
			CHECK(surface_at(&g, h.pins[i].x, h.pins[i].y) == SURF_GREEN);
		for (i = 0; i < h.tee_count; i++)
			CHECK(surface_at(&g, h.tees[i].pos.x, h.tees[i].pos.y) == SURF_TEE);
		CHECK(surface_at(&g, h.aim.x, h.aim.y) != SURF_OOB);
	}
	CHECK(a.count == c.hole_count + 1 + (meshes ? 1 + meshes : 0));
	printf("%s: %d holes, %u bytes in hole records, %d terrain meshes\n", a.name, c.hole_count, total, meshes);
	for (i = 0; i < a.count; i++) free(a.rec[i]);
	for (i = 0; i < b.count; i++) free(b.rec[i]);
	printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}

static void terrain_directory(void)
{
	const uint8_t legacy[] = {'M','S','H','1', 0,4, 0,5};
	uint8_t pair[] = {'M','S','H','2', 0,4, 0,5, 0,6, 0,0};

	CHECK(course_terrain_record(legacy, sizeof legacy, 2, 6, 2, 1) == 5);
	CHECK(course_terrain_record(pair, sizeof pair, 2, 7, 1, 0) == 4);
	CHECK(course_terrain_record(pair, sizeof pair, 2, 7, 1, 1) == 5);
	CHECK(course_terrain_record(pair, sizeof pair, 2, 7, 2, 1) == 6);
	CHECK(!course_terrain_record(pair, sizeof pair - 1, 2, 7, 1, 1));
	CHECK(!course_terrain_record(pair, sizeof pair, 2, 8, 1, 0));
	pair[7] = 0;
	pair[9] = 5;
	CHECK(!course_terrain_record(pair, sizeof pair, 2, 6, 1, 0));
	pair[5] = 0;
	pair[7] = 4;
	CHECK(!course_terrain_record(pair, sizeof pair, 2, 6, 1, 1));
	pair[5] = 5;
	pair[9] = 6;
	CHECK(!course_terrain_record(pair, sizeof pair, 2, 7, 1, 0));
}

static void mesh_record(const uint8_t *rec, uint32_t len)
{
	static const uint8_t mesh[] = {
		0, 0, 0, 0, 0, 2, 0, 2,
		0, 0, 0, 4, 0, 6,
		0, 16 | SURF_ROUGH, 0, 32 | SURF_GREEN, 0, 32 | SURF_BUNKER
	};
	Hole h;
	uint8_t *b, *tail;

	if (hole_parse(&h, rec, len) != HOLE_OK) return;
	if (h.green_mesh) len = (uint32_t)(h.green_mesh - rec);
	if (h.putting_curves) len = (uint32_t)(h.putting_curves - rec);
	b = malloc(len + sizeof mesh);
	memcpy(b, rec, len);
	tail = b + len;
	memcpy(tail, mesh, sizeof mesh);
	b[1] = 1;
	CHECK(hole_parse(&h, b, len) == HOLE_OK && !h.green_mesh);
	b[1] = 2;
	CHECK(hole_parse(&h, b, len + sizeof mesh) == HOLE_OK && h.green_mesh == tail);
	CHECK(hole_parse(&h, b, len + sizeof mesh - 1) == HOLE_ERR_SHORT);
	tail[0] = 0xff;
	CHECK(hole_parse(&h, b, len + sizeof mesh) == HOLE_ERR_VALUE);
	tail[0] = mesh[0];
	tail[11] = 3;
	CHECK(hole_parse(&h, b, len + sizeof mesh) == HOLE_ERR_VALUE);
	tail[11] = mesh[11];
	tail[17] = 16 | SURF_GREEN;
	CHECK(hole_parse(&h, b, len + sizeof mesh) == HOLE_ERR_VALUE);
	tail[17] = mesh[17];
	tail[19] = 32 | SURF_COUNT;
	CHECK(hole_parse(&h, b, len + sizeof mesh) == HOLE_ERR_VALUE);
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

	if (argc == 4 && strcmp(argv[1], "--course") == 0) return any_course(argv[2], argv[3]);
	if (argc != 4) {
		printf("usage: coursetest COURSE.pdb ROUNDTRIP.pdb OUT.ppm\n");
		return 2;
	}
	if (!load_pdb(argv[1], &a) || !load_pdb(argv[2], &b)) {
		printf("FAIL can't read the PDBs\n");
		return 1;
	}
	CHECK(strcmp(a.name, "Par386 Test") == 0);
	CHECK(a.count == 4 || a.count == 6 || a.count == 7);
	terrain_directory();

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
	if (a.count > 4) CHECK(terrain_record(&a, &c, &h, 1) == a.count - 5);
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
	bad[1] = HOLE_VERSION + 1;
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
	header_versions(a.rec[0], a.len[0]);
	fuzz(a.rec[1], a.len[1], 1);
	mesh_record(a.rec[1], a.len[1]);

	for (i = 0; i < a.count; i++) free(a.rec[i]);
	for (i = 0; i < b.count; i++) free(b.rec[i]);
	printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
