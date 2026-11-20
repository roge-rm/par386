/* Ball physics test. Run by `make physics-test`:
 *   physicstest COURSE.pdb [OUTDIR]
 * Plays shots on made-up flat and sloped holes and on the test hole and
 * checks the carry of each club against its range, wind, hook and slice,
 * lies, water drops, out of bounds, rolls on slopes, that the same shot
 * always comes out the same, the swing meter's timing and the camera's
 * projection. Prints the carry table. With OUTDIR it saves the strip and
 * overlays over a blank scene as PPMs there. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ball.h"
#include "play.h"
#include "sound.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define YD(v) ((fixed)((v) * 65536.0))
#define TOYD(v) ((v) / 65536.0)

/* The stored checksum over the run of shots in checksum_run. */
#define CHECKSUM 0xdf6148f0UL

/* Carry ranges in yards, longest first, rising down the bag with no overlaps. */
static const int range_lo[CLUB_COUNT - 1] = { 230, 212, 198, 186, 176, 166, 155, 144, 132, 120, 108, 95, 80, 60 };
static const int range_hi[CLUB_COUNT - 1] = { 250, 229, 211, 197, 185, 175, 165, 154, 143, 131, 119, 107, 94, 75 };

static uint8_t *load_file(const char *path, uint32_t *len)
{
	FILE *f = fopen(path, "rb");
	uint8_t *d;
	long n;

	*len = 0;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	d = malloc(n ? n : 1);
	if (fread(d, 1, n, f) != (size_t)n) { free(d); fclose(f); return NULL; }
	fclose(f);
	*len = (uint32_t)n;
	return d;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static uint8_t *pdb_record(const uint8_t *d, uint32_t n, int k, uint32_t *len)
{
	int count = d[76] << 8 | d[77];
	uint32_t a, b;
	uint8_t *r;

	if (k >= count) return NULL;
	a = be32(d + 78 + 8 * k);
	b = k + 1 < count ? be32(d + 78 + 8 * (k + 1)) : n;
	*len = b - a;
	r = malloc(*len);
	memcpy(r, d + a, *len);
	return r;
}

static uint8_t *grid_bytes;
static uint16_t grid_stride;

static void put_row(void *ctx, uint16_t row, const uint8_t *bytes, uint16_t len)
{
	(void)ctx;
	memcpy(grid_bytes + (uint32_t)row * grid_stride, bytes, len);
}

/* A made-up hole 256 x 640 yards with 8 yard height cells, 1/16 yard
 * height steps, the heights from slope (bytes per cell across) and
 * everything one surface. */
#define FW 33
#define FH 81
static uint8_t flat_h[FW * FH];
static uint8_t flat_g[64 * 320];
static Hole flat;
static SurfaceGrid fgrid;

static void flat_make(int slope, uint8_t surface)
{
	int i, j;
	memset(&flat, 0, sizeof flat);
	for (j = 0; j < FH; j++)
		for (i = 0; i < FW; i++) flat_h[j * FW + i] = (uint8_t)(64 + slope * (i - 16));
	flat.heights = flat_h;
	flat.base = 0;
	flat.step = FIX_ONE / 16;
	flat.width = 256;
	flat.length = 640;
	flat.hm_w = FW;
	flat.hm_h = FH;
	flat.hm_shift = 3;
	flat.pins[0] = vec2_make(YD(128), YD(600));
	surface_size(&flat, &fgrid);
	memset(flat_g, surface * 0x11, sizeof flat_g);
	fgrid.cells = flat_g;
}

/* Sets the cells of a box of yards to a surface. */
static void flat_box(int x0, int y0, int x1, int y1, uint8_t surface)
{
	int x, y;
	for (y = y0 / 2; y < y1 / 2 && y < fgrid.h; y++)
		for (x = x0 / 2; x < x1 / 2 && x < fgrid.w; x++) {
			uint8_t *b = &flat_g[y * fgrid.stride + x / 2];
			*b = (x & 1) ? (uint8_t)((*b & 0xF0) | surface) : (uint8_t)((*b & 0x0F) | (surface << 4));
		}
}

static ShotIn shot_in(uint8_t club)
{
	ShotIn in;
	memset(&in, 0, sizeof in);
	in.hole = &flat;
	in.grid = &fgrid;
	in.pos = vec2_make(YD(128), YD(20));
	in.pin = flat.pins[0];
	in.aim = ANG_90;
	in.power = FIX_ONE;
	in.club = club;
	in.lie = LIE_FAIRWAY;
	return in;
}

static Shot sh;

static Shot *play(const ShotIn *in)
{
	ball_shot(in, &sh);
	return &sh;
}

static int carry_yd(const ShotIn *in) { return (int)(TOYD(play(in)->carry) + 0.5); }
static double side(const Shot *s) { return TOYD(s->stop.x - s->start.x); }

static void carry_table(void)
{
	int c, last = 10000;
	printf("club  carry  total  in range\n");
	for (c = 0; c < CLUB_COUNT - 1; c++) {
		ShotIn in = shot_in((uint8_t)c);
		Shot *s = play(&in);
		int carry = (int)(TOYD(s->carry) + 0.5), total = (int)(TOYD(s->total) + 0.5), est = ball_carry((uint8_t)c, LIE_FAIRWAY);
		int ok = carry >= range_lo[c] && carry <= range_hi[c];
		printf("%-4s  %5d  %5d  %d-%d %s (table %d)\n", ball_club_name((uint8_t)c), carry, total,
		       range_lo[c], range_hi[c], ok ? "ok" : "OUT", est);
		CHECK(ok);
		CHECK(s->result == SHOT_OK && s->total >= s->carry);
		CHECK(carry < last);
		CHECK(est >= range_lo[c] && est <= range_hi[c]);
		CHECK(abs(est - carry) <= 4);
		CHECK(abs((int)(side(s) * 10)) <= 5);
		last = carry;
		if (c) CHECK(range_hi[c] < range_lo[c - 1]);
	}
	/* the putter rolls only */
	{
		ShotIn in = shot_in(CLUB_PT);
		Shot *s;
		flat_make(0, SURF_GREEN);
		in.lie = LIE_GREEN;
		s = play(&in);
		printf("Pt    %5d  %5d  on a flat green\n", (int)TOYD(s->carry), (int)(TOYD(s->total) + 0.5));
		CHECK(s->carry == 0 && s->fly_n <= 1);
		CHECK(s->total > YD(BALL_PUTT_MAX - 2) && s->total < YD(BALL_PUTT_MAX + 2));
		flat_make(0, SURF_FAIRWAY);
	}
}

static void wind_tests(void)
{
	ShotIn in = shot_in(CLUB_7I);
	int calm = carry_yd(&in), head, tail;
	double right, left;

	in.wind = ball_wind_vec(15, ANG_270);
	head = carry_yd(&in);
	in.wind = ball_wind_vec(15, ANG_90);
	tail = carry_yd(&in);
	in.wind = ball_wind_vec(15, 0);
	right = side(play(&in));
	in.wind = ball_wind_vec(15, ANG_180);
	left = side(play(&in));
	printf("7i, 15 mph: calm %d, headwind %d, tailwind %d, blowing right %+.1f, left %+.1f yd\n",
	       calm, head, tail, right, left);
	CHECK(head < calm - 5 && tail > calm + 3);
	CHECK(right > 3 && left < -3);
	in = shot_in(CLUB_DR);
	calm = carry_yd(&in);
	in.wind = ball_wind_vec(20, ANG_270);
	head = carry_yd(&in);
	printf("Dr, 20 mph headwind: %d (calm %d)\n", head, calm);
	CHECK(head < calm - 15);

	/* wind from a seed: in range, the same each time, changing between holes */
	{
		uint8_t mph, m2, h, same = 0, top = 0;
		angle dir, d2;
		for (h = 1; h <= 18; h++) {
			ball_wind(12345, h, 0, &mph, &dir);
			ball_wind(12345, h, 0, &m2, &d2);
			CHECK(mph <= 20 && mph == m2 && dir == d2);
			ball_wind(12345, (uint8_t)(h + 1), 0, &m2, &d2);
			same += mph == m2 && dir == d2;
			if (mph > top) top = mph;
		}
		CHECK(same < 3 && top > 5);
	}
	/* a course's average: 10 is the usual, 15 blows harder and up to 30 */
	{
		uint8_t mph, m2, h;
		angle dir, d2;
		int sum0 = 0, sum15 = 0, top = 0;
		for (h = 1; h <= 200; h++) {
			ball_wind(777, h, 0, &mph, &dir);
			ball_wind(777, h, 10, &m2, &d2);
			CHECK(mph == m2 && dir == d2);
			sum0 += mph;
			ball_wind(777, h, 15, &m2, &d2);
			CHECK(m2 <= 30 && d2 == dir);
			sum15 += m2;
			if (m2 > top) top = m2;
		}
		printf("wind: mean of 200 holes %.1f mph usual, %.1f at average 15 (top %d)\n", sum0 / 200.0, sum15 / 200.0, top);
		CHECK(sum15 > sum0 + 600 && top > 24);
	}
}

static void snap_tests(void)
{
	static const double e[4] = { 0.2, 0.5, -0.2, -0.5 };
	double d[4];
	int i, c;

	for (c = CLUB_DR; c <= CLUB_9I; c += 7) {
		ShotIn in = shot_in((uint8_t)c);
		for (i = 0; i < 4; i++) {
			in.snap = YD(e[i]);
			d[i] = side(play(&in));
		}
		printf("%s snap error +0.2 %+.1f, +0.5 %+.1f, -0.2 %+.1f, -0.5 %+.1f yd\n", ball_club_name((uint8_t)c), d[0], d[1], d[2], d[3]);
		CHECK(d[0] > 2 && d[1] > d[0] + 2);
		CHECK(d[2] < -2 && d[3] < d[2] - 2);
		in.snap = 0;
		in.mishit = 1;
		{
			Shot *s = play(&in);
			int full = carry_yd(&in);
			printf("  mis-hit: carry %d, %+.1f yd\n", full, side(s));
			CHECK(side(s) < -5);
		}
	}
}

/* The highest point of a shot's path above where it started, in yards. */
static double apex(const Shot *s)
{
	int k, top = 0;
	for (k = 0; k < s->n; k++)
		if (s->path[k].z - s->path[0].z > top) top = s->path[k].z - s->path[0].z;
	return top / 16.0;
}

/* How far the flight curved right (above 0) or left, in yards: four times
 * how far the point halfway through the flight is off the line from the
 * start to the landing, as for a steady sideways pull. */
static double curve(const Shot *s)
{
	int m = s->fly_n / 2;
	double x0 = s->path[0].x / 16.0, y0 = s->path[0].y / 16.0;
	double mx = s->path[m].x / 16.0, my = s->path[m].y / 16.0;
	double lx = TOYD(s->land.x), ly = TOYD(s->land.y);
	double off = mx - (x0 + (lx - x0) / (ly - y0) * (my - y0));
	return -4 * off;
}

typedef struct { double carry, total, apex, side, curve; } Res;

static Res shape_shot(uint8_t club, uint8_t shape, uint8_t mph, angle dir)
{
	ShotIn in = shot_in(club);
	Shot *s;
	Res r;
	in.shape = shape;
	in.wind = ball_wind_vec(mph, dir);
	s = play(&in);
	r.carry = TOYD(s->carry);
	r.total = TOYD(s->total);
	r.apex = apex(s);
	r.side = TOYD(s->land.x - s->start.x);
	r.curve = curve(s);
	CHECK(s->result == SHOT_OK);
	return r;
}

static void shape_tests(void)
{
	static const uint8_t cl[3] = { CLUB_DR, CLUB_7I, CLUB_SW };
	int i, k;

	/* which clubs have which shapes, and the cycle */
	CHECK(ball_shape_ok(CLUB_PT, SHAPE_NORMAL) && !ball_shape_ok(CLUB_PT, SHAPE_DRAW));
	CHECK(ball_shape_next(CLUB_PT, SHAPE_NORMAL) == SHAPE_NORMAL);
	CHECK(!ball_shape_ok(CLUB_DR, SHAPE_PUNCH) && !ball_shape_ok(CLUB_5W, SHAPE_PUNCH) && ball_shape_ok(CLUB_3I, SHAPE_PUNCH));
	CHECK(!ball_shape_ok(CLUB_8I, SHAPE_OPEN) && ball_shape_ok(CLUB_9I, SHAPE_OPEN) && ball_shape_ok(CLUB_LW, SHAPE_OPEN));
	CHECK(ball_shape_next(CLUB_DR, SHAPE_LOW) == SHAPE_NORMAL);
	CHECK(ball_shape_next(CLUB_7I, SHAPE_LOW) == SHAPE_PUNCH && ball_shape_next(CLUB_7I, SHAPE_PUNCH) == SHAPE_NORMAL);
	CHECK(ball_shape_next(CLUB_SW, SHAPE_PUNCH) == SHAPE_OPEN && ball_shape_next(CLUB_SW, SHAPE_OPEN) == SHAPE_NORMAL);
	/* the putter ignores a shape */
	{
		ShotIn in = shot_in(CLUB_PT);
		Shot *s;
		fixed t;
		flat_make(0, SURF_GREEN);
		in.lie = LIE_GREEN;
		t = play(&in)->total;
		in.shape = SHAPE_DRAW;
		s = play(&in);
		CHECK(s->total == t);
		flat_make(0, SURF_FAIRWAY);
	}

	printf("shape  club  carry  total  apex  side  curve (against normal)\n");
	for (k = 0; k < 3; k++) {
		uint8_t c = cl[k];
		Res n = shape_shot(c, SHAPE_NORMAL, 0, 0), r[SHAPE_COUNT];
		for (i = 0; i < SHAPE_COUNT; i++) {
			if (!ball_shape_ok(c, (uint8_t)i)) continue;
			r[i] = shape_shot(c, (uint8_t)i, 0, 0);
			printf("%-5s  %-4s  %+5.1f  %+5.1f  %+4.1f  %+5.1f  %+5.1f   (%.1f %.1f %.1f)\n",
			       i ? ball_shape_name((uint8_t)i) : "Norm", ball_club_name(c),
			       r[i].carry - n.carry, r[i].total - n.total, r[i].apex - n.apex, r[i].side, r[i].curve,
			       r[i].carry, r[i].total, r[i].apex);
		}
		/* draw curves left and fade right (wedges hardly), both ending near the target; draw rolls more */
		CHECK(r[SHAPE_DRAW].curve < (c == CLUB_SW ? -1 : -4) && r[SHAPE_FADE].curve > (c == CLUB_SW ? 1 : 4));
		CHECK(fabs(r[SHAPE_DRAW].side) < 8 && fabs(r[SHAPE_FADE].side) < 8);
		CHECK(r[SHAPE_DRAW].total - r[SHAPE_DRAW].carry > r[SHAPE_FADE].total - r[SHAPE_FADE].carry);
		/* high goes higher, low lower */
		CHECK(r[SHAPE_HIGH].apex > n.apex + 2 && r[SHAPE_LOW].apex < n.apex - 2);
		CHECK(fabs(r[SHAPE_HIGH].side) < 1 && fabs(r[SHAPE_LOW].side) < 1);
		if (ball_shape_ok(c, SHAPE_PUNCH)) {
			/* punch: low, a shorter carry and more roll */
			CHECK(r[SHAPE_PUNCH].apex < r[SHAPE_LOW].apex);
			CHECK(r[SHAPE_PUNCH].carry < n.carry - 10);
			CHECK(r[SHAPE_PUNCH].total - r[SHAPE_PUNCH].carry > n.total - n.carry + 3);
		}
		if (ball_shape_ok(c, SHAPE_OPEN)) {
			/* open: higher, less roll */
			CHECK(r[SHAPE_OPEN].apex > n.apex + 2);
			CHECK(r[SHAPE_OPEN].total - r[SHAPE_OPEN].carry < n.total - n.carry);
		}
	}
	/* the punch stays under the wind: a headwind and a crosswind move it less */
	{
		Res n = shape_shot(CLUB_7I, SHAPE_NORMAL, 0, 0), nh = shape_shot(CLUB_7I, SHAPE_NORMAL, 15, ANG_270);
		Res nx = shape_shot(CLUB_7I, SHAPE_NORMAL, 15, 0);
		Res p = shape_shot(CLUB_7I, SHAPE_PUNCH, 0, 0), ph = shape_shot(CLUB_7I, SHAPE_PUNCH, 15, ANG_270);
		Res px = shape_shot(CLUB_7I, SHAPE_PUNCH, 15, 0);
		printf("7i 15 mph headwind: normal %+.1f yd carry, punch %+.1f; across: normal %+.1f, punch %+.1f\n",
		       nh.carry - n.carry, ph.carry - p.carry, nx.side - n.side, px.side - p.side);
		CHECK(n.carry - nh.carry > 2 * (p.carry - ph.carry));
		CHECK(nx.side - n.side > 2 * (px.side - p.side));
	}
}

/* The carry the panel shows for each club and shape against the flight's. */
static void shape_carry_tests(void)
{
	int c, i;
	printf("shape carry: club, then flight-table for each shape\n");
	for (c = 0; c < CLUB_PT; c++) {
		Res n = shape_shot((uint8_t)c, SHAPE_NORMAL, 0, 0);
		printf("%-4s", ball_club_name((uint8_t)c));
		for (i = 1; i < SHAPE_COUNT; i++) {
			Res r;
			int got, want;
			if (!ball_shape_ok((uint8_t)c, (uint8_t)i)) { printf("     -"); continue; }
			r = shape_shot((uint8_t)c, (uint8_t)i, 0, 0);
			got = (int)(r.carry + 0.5);
			want = ball_shape_carry((uint8_t)c, LIE_FAIRWAY, (uint8_t)i);
			printf(" %+3d/%+3d", (int)(r.carry - n.carry + (r.carry > n.carry ? 0.5 : -0.5)), got - want);
			CHECK(abs(got - want) <= 2);
		}
		printf("\n");
		CHECK(ball_shape_carry((uint8_t)c, LIE_ROUGH, SHAPE_NORMAL) == ball_carry((uint8_t)c, LIE_ROUGH));
	}
	CHECK(ball_shape_carry(CLUB_PT, LIE_GREEN, SHAPE_NORMAL) == 0);
	CHECK(ball_shape_carry(CLUB_7I, LIE_FAIRWAY, SHAPE_PUNCH) < ball_carry(CLUB_7I, LIE_FAIRWAY) - 20);
	/* a shape the club can't play counts as normal */
	CHECK(ball_shape_carry(CLUB_DR, LIE_TEE, SHAPE_PUNCH) == ball_carry(CLUB_DR, LIE_TEE));
}

static void lie_tests(void)
{
	static const uint8_t l[] = { LIE_FAIRWAY, LIE_FIRST_CUT, LIE_ROUGH, LIE_DEEP_ROUGH, LIE_BUNKER, LIE_PLUGGED, LIE_FRINGE, LIE_PATH };
	int k[sizeof l], i;
	ShotIn in = shot_in(CLUB_7I);

	printf("7i carry by lie:");
	for (i = 0; i < (int)sizeof l; i++) {
		in.lie = l[i];
		k[i] = carry_yd(&in);
		printf(" %s %d (%d%%, table %d)", ball_lie_name(l[i]), k[i], k[i] * 100 / k[0], ball_carry(CLUB_7I, l[i]));
		CHECK(abs(ball_carry(CLUB_7I, l[i]) - k[i]) <= 5);
	}
	printf("\n");
	CHECK(k[2] < k[0] - 5 && k[4] < k[0] - 5 && k[3] < k[2] && k[5] < k[4] && k[1] < k[0]);
	for (i = 0; i < LIE_COUNT; i++) CHECK(strlen(ball_lie_name((uint8_t)i)) <= 8);

	/* club picking: longest that doesn't go past, the driver only off the tee */
	CHECK(ball_pick(400, LIE_TEE) == CLUB_DR);
	CHECK(ball_pick(400, LIE_FAIRWAY) == CLUB_3W);
	CHECK(ball_pick(150, LIE_FAIRWAY) == CLUB_7I);
	CHECK(ball_pick(150, LIE_ROUGH) < CLUB_7I);
	CHECK(ball_pick(10, LIE_FAIRWAY) == CLUB_LW);
}

static void water_oob_tests(void)
{
	Shot *s;
	ShotIn in;
	double cross_to_pin;
	uint8_t surf;

	/* a pond across the hole from 140 to 200 yards; the 7 iron lands in it */
	flat_make(0, SURF_FAIRWAY);
	flat_box(0, 140, 256, 200, SURF_WATER);
	in = shot_in(CLUB_7I);
	s = play(&in);
	surf = surface_at(&fgrid, s->stop.x, s->stop.y);
	cross_to_pin = 600 - 140;
	printf("water: landed at %.1f, dropped at %.1f, %.1f yd from the pin\n", TOYD(s->land.y), TOYD(s->stop.y),
	       TOYD(vec2_len(vec2_sub(s->stop, in.pin))));
	CHECK(s->result == SHOT_WATER);
	CHECK(surf != SURF_WATER && surf != SURF_OOB);
	CHECK(s->stop.y < YD(140) && s->stop.y > YD(130));
	CHECK(TOYD(vec2_len(vec2_sub(s->stop, in.pin))) >= cross_to_pin - 0.5);

	/* a pond off at an angle: the drop is on the line from the pin through the crossing */
	flat_make(0, SURF_FAIRWAY);
	flat_box(150, 100, 256, 240, SURF_WATER);
	in = shot_in(CLUB_7I);
	in.aim = ANG_90 - 2500;
	s = play(&in);
	surf = surface_at(&fgrid, s->stop.x, s->stop.y);
	printf("water to the side: landed %.1f,%.1f, dropped %.1f,%.1f\n", TOYD(s->land.x), TOYD(s->land.y), TOYD(s->stop.x), TOYD(s->stop.y));
	CHECK(s->result == SHOT_WATER && surf != SURF_WATER && surf != SURF_OOB);
	CHECK(s->stop.x < YD(150) && s->stop.x > YD(140));

	/* out of bounds right of 150 yards across: replay from the same spot and lie */
	flat_make(0, SURF_FAIRWAY);
	flat_box(150, 0, 256, 640, SURF_OOB);
	in = shot_in(CLUB_DR);
	in.lie = LIE_TEE;
	in.aim = ANG_90 - 3000;
	s = play(&in);
	printf("out of bounds: landed %.1f,%.1f\n", TOYD(s->land.x), TOYD(s->land.y));
	CHECK(s->result == SHOT_OOB);
	CHECK(s->stop.x == in.pos.x && s->stop.y == in.pos.y && s->lie == LIE_TEE);

	/* off the hole altogether is out of bounds too */
	flat_make(0, SURF_FAIRWAY);
	in = shot_in(CLUB_DR);
	in.pos = vec2_make(YD(20), YD(20));
	in.aim = ANG_180 - 3000;
	CHECK(play(&in)->result == SHOT_OOB);

	/* a plugged lie from a wedge into a bunker */
	flat_make(0, SURF_BUNKER);
	in = shot_in(CLUB_SW);
	s = play(&in);
	CHECK(s->result == SHOT_OK && s->lie == LIE_PLUGGED);
	CHECK(s->total - s->carry < YD(1));
	/* a low runner into sand stops near where it lands */
	in = shot_in(CLUB_3I);
	in.power = YD(0.3);
	s = play(&in);
	CHECK(s->lie == LIE_BUNKER || s->lie == LIE_PLUGGED);
	CHECK(s->total - s->carry < YD(4));
	flat_make(0, SURF_FAIRWAY);
}

static void slope_tests(void)
{
	static const struct { int slope; uint8_t surf; const char *name; } t[3] = {
		{ 4, SURF_GREEN, "3% green" }, { 10, SURF_FAIRWAY, "8% fairway" }, { 6, SURF_FRINGE, "5% fringe" }
	};
	int i;
	for (i = 0; i < 3; i++) {
		ShotIn in;
		Shot *s;
		flat_make(t[i].slope, t[i].surf);
		in = shot_in(CLUB_PT);
		in.lie = ball_lie(t[i].surf);
		in.power = i ? FIX_ONE : YD(0.6);
		s = play(&in);
		printf("roll on a %s: stopped %.1f yd downhill after %.1f s\n", t[i].name, -side(s), s->steps / 32.0);
		CHECK(s->result == SHOT_OK);
		CHECK(s->steps < 32 * 30);
		CHECK(side(s) < -0.3);
	}
	/* a ball left still on a slope it can't hold rolls down to the flat */
	{
		ShotIn in;
		Shot *s;
		flat_make(40, SURF_GREEN);
		in = shot_in(CLUB_PT);
		in.lie = LIE_GREEN;
		in.power = YD(0.05);
		s = play(&in);
		printf("on a steep green it rolls %.1f yd down\n", -side(s));
		CHECK(s->result == SHOT_OK && s->steps < 32 * 40 && side(s) < -5);
	}
	flat_make(0, SURF_FAIRWAY);
}

/* A putt on the made-up hole from (128, 20) toward the pin at (px, py),
 * aimed off by off (binary angle units), rolling d yards on a flat green. */
static Shot *putt(double px, double py, double d, int off)
{
	ShotIn in = shot_in(CLUB_PT);
	in.lie = LIE_GREEN;
	in.pin = vec2_make(YD(px), YD(py));
	in.aim = (angle)(vec2_angle(vec2_sub(in.pin, in.pos)) + off);
	in.range = YD(BALL_PUTT_MAX);
	in.power = YD(d / BALL_PUTT_MAX);
	return play(&in);
}

static double dist_to(const Shot *s, double px, double py)
{
	double dx = TOYD(s->stop.x) - px, dy = TOYD(s->stop.y) - py;
	return sqrt(dx * dx + dy * dy);
}

static void putt_tests(void)
{
	static const double lens[4] = { 2, 6, 15, 30 };
	int i, k, holed, best;
	Shot *s;

	/* on a flat green the putt rolls power times the range */
	flat_make(0, SURF_GREEN);
	for (i = 0; i < 4; i++) {
		s = putt(200, 600, lens[i], 0);
		printf("flat putt of %.0f yd: rolled %.2f in %.1f s, %d points\n", lens[i], TOYD(s->total), s->steps / 32.0, s->n);
		CHECK(s->result == SHOT_OK && s->lie == LIE_GREEN && !s->lip);
		CHECK(fabs(TOYD(s->total) - lens[i]) < 0.03 * lens[i] + 0.1);
	}
	/* the range sets the scale: half power of 10 yd is 5 */
	{
		ShotIn in = shot_in(CLUB_PT);
		in.lie = LIE_GREEN;
		in.range = YD(10);
		in.power = YD(0.5);
		s = play(&in);
		CHECK(fabs(TOYD(s->total) - 5) < 0.2);
	}

	/* holing out: rolled to stop a foot past, dead straight or a little off */
	s = putt(128, 26, 6.33, 0);
	printf("6 yd putt, a foot past: %s, path %d points, last z %.2f\n", s->result == SHOT_HOLED ? "holed" : "missed", s->n, s->path[s->n - 1].z / 16.0);
	CHECK(s->result == SHOT_HOLED && s->stop.x == YD(128) && s->stop.y == YD(26));
	CHECK(s->path[s->n - 1].z < s->path[s->n - 2].z);
	CHECK(s->lie == LIE_GREEN);
	/* 0.04 yd off at the cup (0.4 degrees over 6 yd) still drops, 0.1 yd off misses by */
	s = putt(128, 26, 6.33, 70);
	CHECK(s->result == SHOT_HOLED);
	s = putt(128, 26, 6.33, 175);
	CHECK(s->result == SHOT_OK && !s->lip);
	/* a putt that stops on the lip, short by under the cup's radius, drops */
	s = putt(128, 26, 5.99, 0);
	printf("dying at the lip: %s, %.3f yd from the cup\n", s->result == SHOT_HOLED ? "holed" : "missed", dist_to(s, 128, 26));
	CHECK(s->result == SHOT_HOLED);
	/* short by an inch more than that stays out */
	s = putt(128, 26, 5.85, 0);
	CHECK(s->result == SHOT_OK && dist_to(s, 128, 26) > 0.05);

	/* too fast: a lip-out off the edge, turned aside, and over the middle when far too fast */
	s = putt(128, 26, 12, 95);
	printf("hit 6 yd past, off the edge: %s, lip %d, stopped %.2f yd aside\n", s->result == SHOT_HOLED ? "holed" : "missed", s->lip, fabs(TOYD(s->stop.x) - 128));
	CHECK(s->result == SHOT_OK && s->lip);
	CHECK(fabs(TOYD(s->stop.x) - 128) > 0.5);
	s = putt(128, 26, 30, 0);
	CHECK(s->result == SHOT_OK && s->lip);
	CHECK(TOYD(s->stop.y) > 40);
	/* firm through the middle (2 yd past) drops, 4 yd past lips out */
	s = putt(128, 26, 7.2, 0);
	CHECK(s->result == SHOT_HOLED);
	s = putt(128, 26, 10.5, 0);
	CHECK(s->result == SHOT_OK && s->lip);

	/* a 3% slope down to the left: the straight putt breaks low and misses, one aimed up the hill drops */
	flat_make(4, SURF_GREEN);
	s = putt(128, 30, 10.4, 0);
	printf("10 yd across a 3%% slope aimed straight: missed %.2f yd low\n", 128 - TOYD(s->stop.x));
	CHECK(s->result == SHOT_OK && TOYD(s->stop.x) < 127.8);
	holed = 0;
	best = 0;
	for (k = 0; k <= 3000 && !holed; k += 40) {
		s = putt(128, 30, 10.4, -k);
		if (s->result == SHOT_HOLED) { holed = 1; best = k; }
	}
	printf("  holed aimed %.1f degrees up the hill\n", best * 360.0 / 65536);
	CHECK(holed && best > 0);
	/* up and down the hill with the flat strength: short going up, long coming down */
	s = putt(140, 20, 10, 0);
	{
		double up = TOYD(s->total);
		s = putt(116, 20, 10, 0);
		printf("10 yd strength up a 3%% slope: %.1f, down it: %.1f\n", up, TOYD(s->total));
		CHECK(up < 9.5 && TOYD(s->total) > 10.5);
	}
	flat_make(0, SURF_FAIRWAY);
}

static uint32_t mix(uint32_t h, uint32_t v)
{
	return (h ^ v) * 16777619UL;
}

static uint32_t checksum_run(const Hole *h, const SurfaceGrid *g)
{
	uint32_t sum = 2166136261UL;
	int i;
	for (i = 0; i < 60; i++) {
		ShotIn in;
		const Shot *s;
		int k;
		memset(&in, 0, sizeof in);
		in.hole = i & 1 ? h : &flat;
		in.grid = i & 1 ? g : &fgrid;
		in.pos = i & 1 ? h->tees[0].pos : vec2_make(YD(128), YD(20));
		in.pin = i & 1 ? h->pins[0] : flat.pins[0];
		in.aim = (angle)(ANG_90 + (i * 397) % 4000 - 2000);
		in.club = (uint8_t)(i % CLUB_COUNT);
		in.lie = (uint8_t)(i % LIE_COUNT);
		in.power = YD(0.4) + (fixed)(i * 7919) % YD(0.6);
		in.snap = (fixed)(i * 4973) % YD(1.6) - YD(0.8);
		in.wind = ball_wind_vec((uint8_t)(i % 21), (angle)(i * 9001));
		in.mishit = i % 13 == 0;
		s = play(&in);
		sum = mix(sum, (uint32_t)s->stop.x);
		sum = mix(sum, (uint32_t)s->stop.y);
		sum = mix(sum, (uint32_t)s->carry);
		sum = mix(sum, (uint32_t)s->n << 8 | s->result << 4 | s->lie);
		for (k = 0; k < s->n; k++) sum = mix(sum, (uint32_t)(uint16_t)s->path[k].z);
	}
	return sum;
}

static void test_hole(const Hole *h, const SurfaceGrid *g)
{
	ShotIn in;
	Shot *s;
	memset(&in, 0, sizeof in);
	in.hole = h;
	in.grid = g;
	in.pos = h->tees[0].pos;
	in.pin = h->pins[0];
	in.aim = vec2_angle(vec2_sub(h->aim, in.pos));
	in.power = FIX_ONE;
	in.club = CLUB_DR;
	in.lie = LIE_TEE;
	s = play(&in);
	printf("test hole drive: carry %.0f, total %.0f, landed %.1f,%.1f on %d, stopped %.1f,%.1f, lie %s\n",
	       TOYD(s->carry), TOYD(s->total), TOYD(s->land.x), TOYD(s->land.y), s->land_surf,
	       TOYD(s->stop.x), TOYD(s->stop.y), ball_lie_name(s->lie));
	CHECK(s->result == SHOT_OK && s->land_surf == SURF_FAIRWAY);
	CHECK(s->lie == LIE_FAIRWAY || s->lie == LIE_FIRST_CUT);
}

static const char *outdir;

/* Saves a 2-bit surface as a PPM in outdir, if there is one. */
static void save(const Surface *s, const char *name)
{
	char path[512];
	FILE *f;
	int x, y;
	if (!outdir) return;
	snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
	f = fopen(path, "wb");
	if (!f) return;
	fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
	for (y = 0; y < s->h; y++)
		for (x = 0; x < s->w; x++) {
			int v = 255 - 85 * ((s->bits[y * s->rowBytes + x / 4] >> (6 - 2 * (x & 3))) & 3);
			fputc(v, f); fputc(v, f); fputc(v, f);
		}
	fclose(f);
}

static void surface2(Surface *s)
{
	memset(s, 0, sizeof *s);
	s->scale = 1;
	s->depth = 2;
	s->w = s->h = 160;
	s->rowBytes = 40;
	s->bits = calloc(40, 160);
	gfx_init(s);
}

/* Plays the swing through: start at t, power at t + up ms, snap at off ms from the snap line. */
static void swing(Play *p, Surface *s, Surface *bg, uint32_t t, uint32_t up, int32_t off)
{
	uint32_t ts;
	CHECK(play_key(p, KEY_SELECT, t) & PL_ANIM);
	CHECK(p->state == PS_UP);
	play_frame(p, s, bg, t + up / 2);
	save(s, "meter-up");
	CHECK(play_tap(p, 80, 60, t + up) & PL_ANIM);
	CHECK(p->state == PS_DOWN);
	play_frame(p, s, bg, t + up + 100);
	ts = t + up + (uint32_t)up * MTR_DOWN_MS / MTR_UP_MS;
	play_key(p, KEY_SELECT, (uint32_t)((int32_t)ts + off));
}

/* A select key press through the gate, as main.c does: play_key only if it counts. */
#define S16_T (MTR_SNAP * 16)

static int gated(Play *p, KeyGate *k, uint16_t code, uint8_t repeat, uint8_t up, uint32_t t)
{
	if (!gate_press(k, code, code == 0x13D ? 0x136 : 0, repeat, up, t)) return -1;
	return play_key(p, KEY_SELECT, t);
}

/* One press counts once: the TX centre's two codes, auto-repeats, a held key, bounce;
 * a quick real second press still counts. And the meter's clock starts once its
 * first frames are on the screen (play_shown). */
static void press_tests(const Hole *h, const SurfaceGrid *g)
{
	Play p;
	Surface s, bg;
	KeyGate k;
	uint32_t t;
	const uint16_t TX = 0x13D, ROCK = 0x136;

	/* the gate on its own */
	memset(&k, 0, sizeof k);
	CHECK(gate_press(&k, TX, ROCK, 0, 1, 1000) == 1);
	CHECK(gate_press(&k, ROCK, 0, 0, 0, 1010) == 0);         /* the same press's second code */
	CHECK(gate_press(&k, TX, ROCK, 1, 1, 1300) == 0);           /* auto-repeat */
	CHECK(gate_press(&k, TX, ROCK, 0, 1, 1300) == 1);           /* no release seen yet on this device: counts */
	gate_release(&k, TX, ROCK);
	CHECK(gate_press(&k, TX, ROCK, 0, 1, 1320) == 0);           /* bounce */
	CHECK(gate_press(&k, TX, ROCK, 0, 1, 1400) == 1);           /* a quick real second press */
	CHECK(gate_press(&k, TX, ROCK, 0, 1, 1700) == 0);           /* held: no release came */
	CHECK(gate_press(&k, ROCK, 0, 0, 0, 1750) == 1);         /* another key, past the pair window */
	CHECK(gate_press(&k, TX, ROCK, 0, 1, 3500) == 1);
	CHECK(gate_press(&k, TX, ROCK, 0, 1, 3600) == 0);
	CHECK(gate_press(&k, TX, ROCK, 0, 1, 3500 + GATE_HELD_MS + 10) == 1);    /* a release that never came */
	CHECK(gate_press(&k, 'x', 0, 0, 0, 6000) == 1);
	/* a release only frees the key that's held; a flush frees it whatever came */
	CHECK(gate_press(&k, TX, ROCK, 0, 1, 8000) == 1);
	gate_release(&k, 'x', 0);
	CHECK(gate_press(&k, TX, ROCK, 0, 1, 8300) == 0);
	gate_release(&k, 0x205, 0x205);
	CHECK(gate_press(&k, TX, ROCK, 0, 1, 8400) == 0);
	gate_flush(&k);
	CHECK(gate_press(&k, TX, ROCK, 0, 1, 8500) == 1);       /* the release was flushed with the keys while the green view drew */
	gate_release(&k, 0, ROCK);                              /* by key code */
	CHECK(gate_press(&k, TX, ROCK, 0, 1, 8600) == 1);

	surface2(&s);
	surface2(&bg);

	/* a swing with the TX's events: each press sends both codes, the power press is held with
	 * repeats and an unflagged copy; it's still at PS_DOWN until the real third press */
	memset(&k, 0, sizeof k);
	play_init(&p, h, g, 7);
	play_frame(&p, &s, &bg, 0);
	CHECK(gated(&p, &k, TX, 0, 1, 1000) & PL_ANIM);
	CHECK(gated(&p, &k, ROCK, 0, 0, 1005) == -1);
	gate_release(&k, TX, ROCK);
	play_frame(&p, &s, &bg, 1060);
	CHECK(gated(&p, &k, TX, 0, 1, 1800) & PL_ANIM);
	CHECK(p.state == PS_DOWN);
	CHECK(gated(&p, &k, ROCK, 0, 0, 1805) == -1);
	CHECK(gated(&p, &k, TX, 1, 1, 2100) == -1);
	CHECK(gated(&p, &k, TX, 0, 1, 2200) == -1);
	play_frame(&p, &s, &bg, 2250);
	CHECK(p.state == PS_DOWN);
	gate_release(&k, TX, ROCK);
	CHECK(gated(&p, &k, TX, 0, 1, 1800 + 600) & PL_ANIM);
	CHECK(p.state == PS_FLY && p.power > YD(0.79) && p.power < YD(0.81) && p.snap == 0);

	/* a quick real double press at the top: power, then snap 120 ms later */
	play_init(&p, h, g, 7);
	play_frame(&p, &s, &bg, 0);
	CHECK(gated(&p, &k, TX, 0, 1, 10000) & PL_ANIM);
	gate_release(&k, TX, ROCK);
	CHECK(gated(&p, &k, TX, 0, 1, 10000 + MTR_UP_MS * 16 / 100) & PL_ANIM);
	gate_release(&k, TX, ROCK);
	CHECK(p.state == PS_DOWN);
	CHECK(gated(&p, &k, TX, 0, 1, 10000 + MTR_UP_MS * 16 / 100 + 120) & PL_ANIM);
	CHECK(p.state == PS_FLY && p.snap == 0);

	/* the meter runs out at the top and turns back: a press within MTR_GRACE_MS is the power press at
	 * full and the way down starts from it; a later one is the snap */
	play_init(&p, h, g, 7);
	play_frame(&p, &s, &bg, 0);
	CHECK(play_key(&p, KEY_SELECT, 30000) & PL_ANIM);
	play_frame(&p, &s, &bg, 30500);
	play_frame(&p, &s, &bg, 30000 + MTR_UP_MS + 20);
	CHECK(p.state == PS_DOWN && p.autoturn && p.t1 == 30000 + MTR_UP_MS);
	CHECK(play_key(&p, KEY_SELECT, 30000 + MTR_UP_MS + 100) & PL_ANIM);
	CHECK(p.state == PS_DOWN && !p.autoturn && p.t1 == 30000 + MTR_UP_MS + 100 && p.power == FIX_ONE);
	play_frame(&p, &s, &bg, 30000 + MTR_UP_MS + 400);
	CHECK(play_key(&p, KEY_SELECT, 30000 + MTR_UP_MS + 100 + MTR_DOWN_MS) & PL_ANIM);
	CHECK(p.state == PS_FLY && p.snap == 0 && p.power == FIX_ONE && !p.mishit);
	play_init(&p, h, g, 7);
	play_frame(&p, &s, &bg, 0);
	CHECK(play_key(&p, KEY_SELECT, 40000) & PL_ANIM);
	play_frame(&p, &s, &bg, 40000 + MTR_UP_MS + 10);
	CHECK(p.state == PS_DOWN && p.autoturn);
	CHECK(play_key(&p, KEY_SELECT, 40000 + MTR_UP_MS + MTR_GRACE_MS + 10) & PL_ANIM);
	CHECK(p.state == PS_FLY && p.snap == FIX_ONE);
	/* the grace runs from when full shows: a slow frame doesn't use it up */
	play_init(&p, h, g, 7);
	play_frame(&p, &s, &bg, 0);
	CHECK(play_key(&p, KEY_SELECT, 45000) & PL_ANIM);
	p.mfirst = 0;
	play_frame(&p, &s, &bg, 45000 + MTR_UP_MS + 10);
	CHECK(p.autoturn == 1 && p.tg == 45000 + MTR_UP_MS + 10);
	play_shown(&p, 45000 + MTR_UP_MS + 200);
	CHECK(p.autoturn == 2 && p.tg == 45000 + MTR_UP_MS + 200);
	CHECK(play_key(&p, KEY_SELECT, 45000 + MTR_UP_MS + 300) & PL_ANIM);
	CHECK(p.state == PS_DOWN && p.power == FIX_ONE && p.t1 == 45000 + MTR_UP_MS + 300);
	/* a power press before the top turns it as usual, so no grace after it */
	play_init(&p, h, g, 7);
	play_frame(&p, &s, &bg, 0);
	CHECK(play_key(&p, KEY_SELECT, 50000) & PL_ANIM);
	CHECK(play_key(&p, KEY_SELECT, 50000 + MTR_UP_MS - 20) & PL_ANIM);
	CHECK(p.state == PS_DOWN && !p.autoturn);
	CHECK(play_key(&p, KEY_SELECT, 50000 + MTR_UP_MS + 40) & PL_ANIM);
	CHECK(p.state == PS_FLY);

	/* the meter's clock: the first frame draws the meter empty and leaves the golfer at address,
	 * the second has his backswing; each shown frame starts the clock again */
	play_init(&p, h, g, 7);
	{
		RenderGolfer gf;
		memset(&gf, 0, sizeof gf);
		p.golf = &gf;             /* no art: pose() does nothing, which this part doesn't need */
		play_frame(&p, &s, &bg, 0);
		p.golf = 0;
	}
	CHECK(play_key(&p, KEY_SELECT, 20000) & PL_ANIM);
	CHECK(p.mfirst == 2);
	CHECK(play_frame(&p, &s, &bg, 20010) & PL_DIRTY);
	CHECK(p.mpos >= S16_T && p.mpos < S16_T + 16 * 2);
	{
		int16_t m1 = p.mpos, m2;
		play_shown(&p, 20300);                     /* the first frame took 300 ms to show */
		CHECK(p.mfirst == 1 && p.t0 <= 20300 && p.t0 >= 20300 - 20);
		play_frame(&p, &s, &bg, 20300);
		CHECK(p.mpos >= m1 && p.mpos < S16_T + 16 * 2);   /* where it showed, not back, not on */
		play_frame(&p, &s, &bg, 20310);
		m2 = p.mpos;
		play_shown(&p, 20400);                     /* the second (his backswing) took 90 ms */
		CHECK(p.mfirst == 0);
		play_frame(&p, &s, &bg, 20400);
		CHECK(p.mpos >= m2 && p.mpos <= m2 + 1);   /* never steps back */
		play_shown(&p, 20500);                     /* no more restarts */
		CHECK(p.mfirst == 0);
	}
	t = p.t0;
	play_frame(&p, &s, &bg, t + MTR_UP_MS / 2);
	CHECK(p.state == PS_UP);
	CHECK(play_key(&p, KEY_SELECT, t + MTR_UP_MS * 4 / 5) & PL_ANIM);
	CHECK(p.power > YD(0.79) && p.power < YD(0.81));
	free(s.bits);
	free(bg.bits);
}

static void meter_tests(const Hole *h, const SurfaceGrid *g)
{
	Play p;
	Surface s, bg;
	uint32_t t;
	int f, frames;
	int16_t sx, sy;
	fixed depth;

	surface2(&s);
	surface2(&bg);
	play_init(&p, h, g, 7);
	CHECK(p.club == CLUB_DR && p.lie == LIE_TEE && p.stroke == 1 && p.state == PS_AIM);
	CHECK(p.wind_mph <= 20);
	f = play_frame(&p, &s, &bg, 0);
	CHECK(f & PL_DIRTY);

	/* the ball projects where render_scene draws it: the middle column, row 127 (BALL_GAP above the bottom of the view) */
	CHECK(render_point(&p.cam, p.rv.ball.x, p.rv.ball.y, hole_height(h, p.rv.ball.x, p.rv.ball.y), &sx, &sy, &depth));
	CHECK(sx == 80 && sy >= 125 && sy <= 129);

	/* club changes and aiming */
	play_key(&p, KEY_DOWN, 10);
	CHECK(p.club == CLUB_3W);
	play_key(&p, KEY_UP, 20);
	CHECK(p.club == CLUB_DR);
	play_key(&p, KEY_LEFT, 30);
	CHECK(p.aim == (angle)(p.rv.aim + 182));
	CHECK(!(play_frame(&p, &s, &bg, 40) & PL_RENDER));
	play_tap(&p, 100, 60, 50);
	CHECK((int16_t)(p.aim - p.rv.aim) < -500 && (int16_t)(p.aim - p.rv.aim) > -3000);
	CHECK(!(play_frame(&p, &s, &bg, 60) & PL_RENDER));
	/* far off the side the scene is drawn again looking that way */
	play_tap(&p, 2, 60, 70);
	CHECK(play_frame(&p, &s, &bg, 80) & PL_RENDER);
	CHECK(p.rv.aim == p.aim);
	play_frame(&p, &s, &bg, 90);
	/* shapes: the key goes round the driver's, a tap on the panel's button does the same, the putter has none */
	CHECK(p.shape == SHAPE_NORMAL);
	play_key(&p, KEY_SHAPE, 91);
	CHECK(p.shape == SHAPE_DRAW);
	play_frame(&p, &s, &bg, 92);
	save(&s, "strip-draw");
	play_tap(&p, 75, 141, 93);
	CHECK(p.shape == SHAPE_FADE && p.state == PS_AIM);
	play_key(&p, KEY_SHAPE, 94);
	play_key(&p, KEY_SHAPE, 95);
	CHECK(p.shape == SHAPE_LOW);
	play_key(&p, KEY_SHAPE, 96);
	CHECK(p.shape == SHAPE_NORMAL);
	play_key(&p, KEY_SHAPE, 97);
	play_key(&p, KEY_DOWN, 98);
	CHECK(p.club == CLUB_3W && p.shape == SHAPE_DRAW);
	for (f = 0; f < 15; f++) play_key(&p, KEY_DOWN, 99);
	CHECK(p.club == CLUB_PT && p.shape == SHAPE_NORMAL);
	play_key(&p, KEY_SHAPE, 100);
	CHECK(p.shape == SHAPE_NORMAL);
	/* the map key opens the map and closes it again */
	play_init(&p, h, g, 7);
	play_frame(&p, &s, &bg, 100);
	CHECK(play_key(&p, KEY_MAP, 101) == PL_MAP && p.view == PLAY_MAP);
	CHECK(play_key(&p, KEY_MAP, 102) == PL_SCENE && p.view == PLAY_SCENE);
	play_frame(&p, &s, &bg, 103);

	/* 80 % power and a perfect snap, as a draw; the next shot is back to normal */
	play_key(&p, KEY_SHAPE, 104);
	CHECK(p.shape == SHAPE_DRAW);
	swing(&p, &s, &bg, 1000, MTR_UP_MS * 4 / 5, 0);
	printf("meter: power %.3f snap %.3f\n", TOYD(p.power), TOYD(p.snap));
	CHECK(p.power > YD(0.79) && p.power < YD(0.81));
	CHECK(p.snap == 0 && p.state == PS_FLY && !p.mishit);
	/* the flight plays out, the result shows, then the next shot */
	frames = 0;
	for (t = 3000; t < 40000; t += 30) {
		f = play_frame(&p, &s, &bg, t);
		frames++;
		if (frames == 25) save(&s, "flight");
		if (p.state == PS_RESULT) save(&s, "result");
		if (f & PL_RENDER) break;
	}
	printf("shot 1: %s, then %s; %d frames\n", p.msg1, p.msg2, frames);
	CHECK(f & PL_RENDER);
	CHECK(p.stroke == 2 && p.state == PS_AIM && p.lie != LIE_TEE);
	CHECK(p.club != CLUB_DR && p.shape == SHAPE_NORMAL);
	play_frame(&p, &s, &bg, t + 10);

	/* late and early clicks give a hook and a slice in proportion */
	swing(&p, &s, &bg, 50000, MTR_UP_MS / 2, 50);
	CHECK(p.snap == -YD(0.5));
	play_init(&p, h, g, 7);
	play_frame(&p, &s, &bg, 0);
	swing(&p, &s, &bg, 1000, MTR_UP_MS / 2, -30);
	CHECK(p.snap == YD(0.25));
	/* no clicks after the start: full power, then a mis-hit */
	play_init(&p, h, g, 7);
	play_frame(&p, &s, &bg, 0);
	play_key(&p, KEY_SELECT, 1000);
	for (t = 1000; t < 4000 && p.state != PS_FLY; t += 20) play_frame(&p, &s, &bg, t);
	CHECK(p.state == PS_FLY && p.mishit && p.power == FIX_ONE && p.snap == -FIX_ONE);
	CHECK(t > 1000 + MTR_UP_MS + MTR_DOWN_MS);
	/* the map with the path, and back */
	for (; t < 40000 && p.state != PS_RESULT; t += 30) play_frame(&p, &s, &bg, t);
	CHECK(play_key(&p, KEY_OTHER, t) == 0);
	CHECK(play_frame(&p, &s, &bg, t + 2000) & PL_RENDER);
	CHECK(play_key(&p, KEY_OTHER, t + 2100) == PL_MAP);
	play_map(&s, &p);
	CHECK(play_tap(&p, 80, 154, t + 2200) == PL_SCENE);
	CHECK(play_frame(&p, &s, &bg, t + 2300) & PL_DIRTY);
	free(s.bits);
	free(bg.bits);
}

/* Puts the ball where a shot stopped and lets play move on to the next one, as after a result. */
static int place(Play *p, Surface *s, Surface *bg, vec2 at, uint8_t lie, uint32_t t)
{
	p->have_shot = 1;
	memset(&p->shot, 0, sizeof p->shot);
	p->shot.result = SHOT_OK;
	p->shot.start = p->shot.stop = at;
	p->shot.lie = lie;
	p->shot.n = 1;
	p->shown = 0;
	p->state = PS_RESULT;
	p->t0 = t;
	return play_frame(p, s, bg, t + 1600);
}

/* Draws the green view into s and keeps it in bg, as the platform does. */
static void green_view(Play *p, Surface *s, Surface *bg)
{
	CHECK(render_green(s, &p->rv) == 0);
	memcpy(bg->bits, s->bits, (size_t)s->rowBytes * s->h);
}

/* A putt through play.c: the power click when the marker reaches the cup's tick, plus late ms. */
static void putt_play(Play *p, Surface *s, Surface *bg, uint32_t t, int32_t late, const char *name)
{
	uint32_t ms = (uint32_t)(((int32_t)p->putt_tick * MTR_UP_MS + (MTR_W - MTR_SNAP) / 2) / (MTR_W - MTR_SNAP) + late);
	int f, k = 0;
	CHECK(play_key(p, KEY_SELECT, t) & PL_ANIM);
	CHECK(p->state == PS_UP);
	play_frame(p, s, bg, t + ms / 2);
	if (name) save(s, "putt-meter");
	play_tap(p, 80, 60, t + ms);
	CHECK(p->state == PS_FLY && p->snap == 0);
	for (t += ms; t < 200000; t += 30) {
		f = play_frame(p, s, bg, t);
		if (name && ++k == 20) save(s, name);
		if (p->state == PS_DONE || (f & PL_RENDER)) break;
	}
}

/* A full drive through play.c with frames every 30 ms, the shot view on the
 * map or not, its map drawn prows rows a frame while the ball flies in the
 * scene (0 never, below 0 that many once the ball is down): when each sound came (st, sid, *ns), how long after the
 * hit it went over to the map (*tmap, 0 for never) and the time it went on
 * to the next shot. The switch fades as the platform does: the map surface
 * gets the frames, then it's copied over. With name, pictures of the
 * switch, the flight and the result. */
static uint32_t drive_run(const Hole *h, const SurfaceGrid *g, uint8_t mapview, uint8_t tap, uint32_t *st, uint8_t *sid, int *ns,
                          uint32_t *tmap, Play *out, const char *name, int16_t prows)
{
	static Play p, q;
	Surface s, bg, m, s2;
	uint32_t t;
	int f, k = 0, logged = 0, shot_res = 0;
	char nm[64];

	surface2(&s);
	surface2(&bg);
	surface2(&m);
	surface2(&s2);
	play_init(&p, h, g, 7);
	p.mapview = mapview;
	play_frame(&p, &s, &bg, 0);
	while (snd_take() != SND_NONE) {}
	snd_log_n = 0;
	swing(&p, &s, &bg, 1000, MTR_UP_MS, 0);
	CHECK(p.state == PS_FLY && p.power == FIX_ONE && p.club == CLUB_DR);
	*ns = 0;
	*tmap = 0;
	for (t = 2760; t < 60000; t += 30) {
		f = play_frame(&p, &s, &bg, t);
		/* prows < 0: the map only starts once the ball is down, too late */
		if (!p.onmap && play_map_wanted(&p) && prows && (prows > 0 || p.shown >= p.shot.fly_n))
			play_map_prep(&m, &p, prows < 0 ? (int16_t)-prows : prows);
		CHECK(!(f & PL_MAP) || p.mprep == 2);
		if (f & PL_MAP) {
			int j;
			CHECK(!*tmap && p.view == PLAY_MAP && p.onmap && p.state == PS_FLY);
			*tmap = t - p.tf;
			/* the map drawn in one go, as on a resize, matches the one drawn in slices */
			q = p;
			play_map(&s2, &q);
			play_frame(&q, &s2, NULL, t);
			play_frame(&p, &m, NULL, t);
			CHECK(memcmp(m.bits, s2.bits, (size_t)m.rowBytes * (m.h - RENDER_STATUS)) == 0);
			/* the fade's steps: the ball goes on over the map */
			for (j = 1; j <= 8; j++) play_frame(&p, &m, NULL, t + 40 * (uint32_t)j);
			play_status(&m, &p, NULL);
			memcpy(s.bits, m.bits, (size_t)m.rowBytes * m.h);
			if (name) {
				snprintf(nm, sizeof nm, "%s-switch", name);
				save(&s, nm);
			}
		}
		/* nothing to do with a key or tap while it flies on the map */
		if (p.onmap && p.state == PS_FLY) CHECK(play_key(&p, KEY_SELECT, t) == 0 && play_tap(&p, 80, 60, t) == 0);
		for (; logged < snd_log_n && *ns < 64; logged++) {
			st[*ns] = t;
			sid[(*ns)++] = snd_log[logged];
		}
		if (name && p.onmap && p.state == PS_FLY && ++k == 80) {
			snprintf(nm, sizeof nm, "%s-flight", name);
			save(&s, nm);
		}
		if (p.state == PS_RESULT && !shot_res) {
			shot_res = 1;
			if (name) {
				snprintf(nm, sizeof nm, "%s-result", name);
				save(&s, nm);
			}
			/* a tap or Select hurries the result on, other keys don't, as on the scene */
			if (mapview) CHECK(play_key(&p, KEY_LEFT, t) == 0 && play_key(&p, KEY_SHAPE, t) == 0 && p.state == PS_RESULT);
			if (tap == 1) CHECK(play_tap(&p, 80, 60, t) == PL_ANIM);
			if (tap == 2) CHECK(play_key(&p, KEY_SELECT, t) == PL_ANIM);
		}
		if (f & PL_RENDER) break;
	}
	CHECK(p.state == PS_AIM && p.view == PLAY_SCENE && !p.onmap && p.stroke == 2);
	*out = p;
	free(s.bits);
	free(bg.bits);
	free(m.bits);
	free(s2.bits);
	return t;
}

/* The shot view on the map: a drive goes over a second after the hit, with
 * the same sounds at the same times and the same result as in the scene;
 * chips and putts stay in the scene. */
static void shotmap_tests(const Hole *h, const SurfaceGrid *g)
{
	static uint32_t sa[64], sb[64];
	static uint8_t ia[64], ib[64];
	static Play a, b, p;
	Surface s, bg;
	uint32_t ta, tb, ma, mb, t, tc;
	int na, nb, k, f, rows;

	ta = drive_run(h, g, 0, 0, sa, ia, &na, &ma, &a, NULL, 8);
	tb = drive_run(h, g, 1, 0, sb, ib, &nb, &mb, &b, "shotmap", 5);
	CHECK(ma == 0 && mb >= PLAY_MAP_AFTER_MS && mb < PLAY_MAP_AFTER_MS + 30);
	CHECK(ta == tb && na == nb && na >= 3);
	for (k = 0; k < na && k < nb; k++) CHECK(sa[k] == sb[k] && ia[k] == ib[k]);
	CHECK(a.rv.ball.x == b.rv.ball.x && a.rv.ball.y == b.rv.ball.y && a.lie == b.lie);
	CHECK(strcmp(a.msg1, b.msg1) == 0 && strcmp(a.msg2, b.msg2) == 0);
	printf("shot view: map %lu ms after the hit, %d sounds at the same times, next shot at %lu ms\n",
	       (unsigned long)mb, nb, (unsigned long)tb);
	/* the map in one go, or so slowly the ball is down first (it stays in the scene), or never: the same shot */
	for (rows = 0; rows < 3; rows++) {
		tc = drive_run(h, g, 1, 0, sb, ib, &nb, &mb, &b, NULL, rows == 0 ? 999 : rows == 1 ? -999 : 0);
		CHECK(tc == ta && nb == na && (rows ? mb == 0 : mb >= PLAY_MAP_AFTER_MS && mb < PLAY_MAP_AFTER_MS + 30));
		for (k = 0; k < na && k < nb; k++) CHECK(sa[k] == sb[k] && ia[k] == ib[k]);
		CHECK(a.rv.ball.x == b.rv.ball.x && a.rv.ball.y == b.rv.ball.y && a.lie == b.lie);
	}
	/* a tap on the result goes on sooner */
	CHECK(drive_run(h, g, 1, 1, sb, ib, &nb, &mb, &b, NULL, 999) < tb);
	CHECK(drive_run(h, g, 1, 2, sb, ib, &nb, &mb, &b, NULL, 999) < tb);

	/* a short chip stays in the scene */
	surface2(&s);
	surface2(&bg);
	play_init(&p, h, g, 7);
	p.mapview = 1;
	play_frame(&p, &s, &bg, 0);
	while (p.club != CLUB_SW) play_key(&p, KEY_DOWN, 10);
	swing(&p, &s, &bg, 1000, MTR_UP_MS / 4, 0);
	CHECK(p.state == PS_FLY && p.shot.fly_n * 42 < PLAY_MAP_MIN_MS);
	for (t = 2000; t < 30000; t += 30) {
		f = play_frame(&p, &s, &bg, t);
		CHECK(!(f & PL_MAP) && !p.onmap);
		if (f & PL_RENDER) break;
	}
	CHECK(p.state == PS_AIM && p.stroke == 2);
	/* a long putt stays too */
	place(&p, &s, &bg, vec2_make(p.rv.hole->pins[0].x - (12L << 16), p.rv.hole->pins[0].y - (25L << 16)), LIE_GREEN, 40000);
	CHECK(p.club == CLUB_PT);
	green_view(&p, &s, &bg);
	play_frame(&p, &s, &bg, 42000);
	CHECK(play_key(&p, KEY_SELECT, 43000) & PL_ANIM);
	for (t = 43000; t < 80000; t += 30) {
		f = play_frame(&p, &s, &bg, t);
		CHECK(!(f & PL_MAP) && !p.onmap);
		if ((f & PL_RENDER) || p.state == PS_DONE) break;
	}
	free(s.bits);
	free(bg.bits);
}

/* Putting on the test hole through play.c: the green view, the meter, holing out and gimmies. */
static void green_tests(const Hole *h, const SurfaceGrid *g)
{
	Play p;
	Surface s, bg;
	vec2 pin = h->pins[0];
	int f, n;
	uint32_t t;

	surface2(&s);
	surface2(&bg);

	/* onto the green 7 yd short of the pin: the putter and the green view */
	play_init(&p, h, g, 7);
	f = place(&p, &s, &bg, vec2_make(pin.x - YD(2), pin.y - YD(7)), LIE_GREEN, 1000);
	CHECK(f & PL_RENDER);
	CHECK(p.club == CLUB_PT && p.rv.green && p.stroke == 2 && p.state == PS_AIM);
	CHECK(p.range == YD(11));
	/* a putt that ends in the water or out of bounds shows the penalty, not the green */
	{
		static const uint8_t res[2] = { SHOT_WATER, SHOT_OOB };
		int k;
		for (k = 0; k < 2; k++) {
			Play q = p;
			memset(&q.shot, 0, sizeof q.shot);
			q.club = CLUB_PT;
			q.shot.result = res[k];
			q.shot.lie = LIE_GREEN;
			q.shot.start = q.shot.stop = q.rv.ball;
			q.shot.n = 1;
			q.shown = -1;
			q.tf = 0;
			q.state = PS_FLY;
			play_frame(&q, &s, &bg, 10);
			CHECK(q.state == PS_RESULT);
			CHECK(strcmp(q.msg1, k == 0 ? "In the water" : "Out of bounds") == 0);
		}
	}
	/* the putter only on the green */
	CHECK(play_key(&p, KEY_UP, 3000) == 0 && p.club == CLUB_PT);
	green_view(&p, &s, &bg);
	play_frame(&p, &s, &bg, 3100);
	/* the ball shows low in the middle and the cup about a third of the way down */
	{
		int16_t sx, sy;
		fixed depth;
		CHECK(render_point(&p.cam, p.rv.ball.x, p.rv.ball.y, hole_height(h, p.rv.ball.x, p.rv.ball.y), &sx, &sy, &depth));
		printf("green view: ball at %d,%d, ", sx, sy);
		CHECK(sx == 80 && sy > 100 && sy < 135);
		CHECK(render_point(&p.cam, pin.x, pin.y, hole_height(h, pin.x, pin.y), &sx, &sy, &depth));
		printf("cup at %d,%d\n", sx, sy);
		CHECK(sx > 60 && sx < 100 && sy > 30 && sy < 70);
	}
	save(&s, "green-aim");
	/* aim with the keys and a tap, and the view stays */
	play_key(&p, KEY_LEFT, 3200);
	CHECK(!(play_frame(&p, &s, &bg, 3300) & PL_RENDER));
	play_tap(&p, 90, 50, 3400);
	CHECK(!(play_frame(&p, &s, &bg, 3500) & PL_RENDER));
	CHECK((int16_t)(p.aim - p.rv.aim) < 0);
	play_init(&p, h, g, 7);
	place(&p, &s, &bg, vec2_make(pin.x - YD(2), pin.y - YD(7)), LIE_GREEN, 1000);
	green_view(&p, &s, &bg);
	play_frame(&p, &s, &bg, 3100);

	/* putts at the cup's tick until it's holed or given */
	t = 10000;
	for (n = 0; n < 4 && p.state != PS_DONE; n++) {
		putt_play(&p, &s, &bg, t, 0, n ? NULL : "putt-roll");
		printf("putt %d: %s, %s\n", n + 1, p.msg1, p.msg2);
		if (p.state != PS_DONE) {
			CHECK(p.rv.green);
			green_view(&p, &s, &bg);
			play_frame(&p, &s, &bg, t + 20000);
		}
		t += 30000;
	}
	save(&s, "putt-done");
	CHECK(p.state == PS_DONE && n <= 3);
	CHECK(strncmp(p.msg1, "In the cup", 10) == 0 || strncmp(p.msg1, "Gimme", 5) == 0);
	CHECK(p.score == p.stroke);
	CHECK(play_key(&p, KEY_SELECT, t) == PL_BACK);
	CHECK(play_tap(&p, 80, 60, t) == PL_BACK);

	/* holed through play.c, straight up the slope from 3 yd below the pin, a
	 * little firmer each time: the score on the strip */
	{
		fixed gx = hole_height(h, pin.x + YD(1), pin.y) - hole_height(h, pin.x - YD(1), pin.y);
		fixed gy = hole_height(h, pin.x, pin.y + YD(1)) - hole_height(h, pin.x, pin.y - YD(1));
		double gl = sqrt((double)gx * gx + (double)gy * gy);
		vec2 at = pin;
		int late;
		if (gl > 0) at = vec2_make(pin.x - (fixed)(3 * 65536.0 * gx / gl), pin.y - (fixed)(3 * 65536.0 * gy / gl));
		for (late = 0; late < 400; late += 20) {
			play_init(&p, h, g, 7);
			place(&p, &s, &bg, at, LIE_GREEN, 1000);
			CHECK(p.putt_tick > (MTR_W - MTR_SNAP) * 3.0 / TOYD(p.range));
			green_view(&p, &s, &bg);
			play_frame(&p, &s, &bg, 3000);
			putt_play(&p, &s, &bg, 5000, late, NULL);
			if (p.shot.result == SHOT_HOLED) break;
		}
		printf("3 yd putt up the slope (%.1f%%): %s, %s, %d ms late\n", gl / 65536.0 / 2 * 100, p.msg1, p.msg2, late);
	}
	CHECK(p.state == PS_DONE && p.shot.result == SHOT_HOLED);
	CHECK(strcmp(p.msg1, "In the cup, 2 shots") == 0 && strcmp(p.msg2, "Eagle") == 0 && p.score == 2);
	save(&s, "putt-holed");

	/* Even the largest grades a course can encode keep the mark on the meter. */
	{
		static const uint8_t heights[] = { 0, 255, 0, 255 };
		Hole steep = *h;
		steep.heights = heights;
		steep.hm_w = steep.hm_h = 2;
		steep.hm_shift = 0;
		steep.step = YD(15);
		steep.pins[0] = vec2_make(YD(1), YD(1));
		play_init(&p, &steep, g, 7);
		place(&p, &s, &bg, vec2_make(0, YD(1)), LIE_GREEN, 1000);
		CHECK(p.putt_tick == MTR_W - MTR_SNAP - 1);
		steep.pins[0].x = 0;
		play_init(&p, &steep, g, 7);
		place(&p, &s, &bg, vec2_make(YD(1), YD(1)), LIE_GREEN, 1000);
		CHECK(p.putt_tick == 0);
	}

	/* left 1.5 ft away: given, one stroke more */
	play_init(&p, h, g, 7);
	p.stroke = 3;
	f = place(&p, &s, &bg, vec2_make(pin.x + YD(0.5), pin.y), LIE_GREEN, 1000);
	printf("gimme: %s, %s\n", p.msg1, p.msg2);
	CHECK(!(f & PL_RENDER) && p.state == PS_DONE && p.score == 4);
	CHECK(strcmp(p.msg1, "Gimme, 4 shots") == 0 && strcmp(p.msg2, "Par") == 0);
	/* 2.5 ft isn't */
	play_init(&p, h, g, 7);
	f = place(&p, &s, &bg, vec2_make(pin.x + YD(0.84), pin.y), LIE_GREEN, 1000);
	CHECK((f & PL_RENDER) && p.state == PS_AIM);
	/* not on the fringe either, where the putter is picked and the other clubs can be */
	play_init(&p, h, g, 7);
	f = place(&p, &s, &bg, vec2_make(pin.x, pin.y - YD(19)), LIE_FRINGE, 1000);
	CHECK((f & PL_RENDER) && p.club == CLUB_PT && p.rv.green);
	CHECK(play_key(&p, KEY_UP, 3000) == PL_RENDER && p.club == CLUB_LW && !p.rv.green);
	CHECK(play_key(&p, KEY_DOWN, 3100) == PL_RENDER && p.club == CLUB_PT && p.rv.green);
	/* the putter runs out of meter: a full putt, no mis-hit */
	play_frame(&p, &s, &bg, 3200);
	play_key(&p, KEY_SELECT, 4000);
	for (t = 4000; t < 7000 && p.state == PS_UP; t += 20) play_frame(&p, &s, &bg, t);
	CHECK(p.state == PS_FLY && p.power == FIX_ONE && !p.mishit);
	/* score names */
	{
		static const struct { int strokes; const char *name; } sc[] = {
			{ 1, "Hole in one!" }, { 3, "Birdie" }, { 5, "Bogey" }, { 6, "Double bogey" }, { 7, "Triple bogey" }, { 9, "5 over par" }
		};
		unsigned k;
		for (k = 0; k < sizeof sc / sizeof sc[0]; k++) {
			play_init(&p, h, g, 7);
			p.stroke = (uint8_t)(sc[k].strokes - 1);
			place(&p, &s, &bg, vec2_make(pin.x + YD(0.3), pin.y), LIE_GREEN, 1000);
			CHECK(strcmp(p.msg2, sc[k].name) == 0);
			CHECK(strlen(p.msg1) <= 20 && strlen(p.msg2) <= 20);
		}
	}
	free(s.bits);
	free(bg.bits);
}

int main(int argc, char **argv)
{
	uint8_t *pdb, *hrec;
	uint32_t n, hlen, a, b;
	Hole h;
	SurfaceGrid g;
	uint8_t row[SURF_ROW_MAX];

	if (argc < 2) {
		printf("usage: physicstest COURSE.pdb [OUTDIR]\n");
		return 2;
	}
	if (argc > 2) outdir = argv[2];
	pdb = load_file(argv[1], &n);
	if (!pdb || n < 78) { printf("FAIL can't read %s\n", argv[1]); return 1; }
	hrec = pdb_record(pdb, n, 1, &hlen);
	CHECK(hrec && hole_parse(&h, hrec, hlen) == HOLE_OK);
	if (fails) return 1;
	surface_size(&h, &g);
	grid_stride = g.stride;
	grid_bytes = malloc((size_t)g.stride * g.h);
	g.cells = grid_bytes;
	surface_raster(&h, row, put_row, &g);

	flat_make(0, SURF_FAIRWAY);
	carry_table();
	wind_tests();
	snap_tests();
	shape_tests();
	shape_carry_tests();
	lie_tests();
	water_oob_tests();
	slope_tests();
	putt_tests();
	test_hole(&h, &g);
	meter_tests(&h, &g);
	press_tests(&h, &g);
	green_tests(&h, &g);
	shotmap_tests(&h, &g);

	a = checksum_run(&h, &g);
	b = checksum_run(&h, &g);
	printf("checksum %08lx\n", (unsigned long)a);
	CHECK(a == b);
	CHECK(a == CHECKSUM);

	free(hrec);
	free(pdb);
	free(grid_bytes);
	printf("physics-test: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
