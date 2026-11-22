/* Rules test. Run by `make rules-test`:
 *   rulestest COURSE.pdb ARTDIR OUTDIR
 * Checks the turn order, the honour, scores and the scorecard totals, the
 * saved round's record and bad records, then plays a 2 player round of the
 * test course through play.c, saving and restoring it on the way. Draws the
 * scorecard, the strips and the clubhouse with Continue round and the
 * players screen at 160x160 2-bit and 320x320 8-bit to OUTDIR as PPMs.
 * Last, the sounds hole 1 of the test course makes and the menu clicks. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "play.h"
#include "sound.h"
#include "front.h"
#include "records.h"
#include "card.h"
#include "cpu.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define YD(v) ((fixed)((v) * 65536.0))

static const char *artdir, *outdir;
static uint8_t pal[768];

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

static uint8_t *load_art(const char *name, uint32_t *len)
{
	char path[512];
	snprintf(path, sizeof path, "%s/%s", artdir, name);
	return load_file(path, len);
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

static void surface(Surface *s, uint8_t scale)
{
	memset(s, 0, sizeof *s);
	s->scale = scale;
	s->depth = scale > 1 ? 8 : 2;
	s->w = s->h = 160 * scale;
	s->rowBytes = (uint16_t)((s->w * s->depth + 15) / 16 * 2);
	s->bits = calloc(s->rowBytes, s->h);
	gfx_init(s);
}

static void save(const Surface *s, const char *name)
{
	char path[512];
	FILE *f;
	int16_t x, y;

	snprintf(path, sizeof path, "%s/%s-%d.ppm", outdir, name, s->w);
	f = fopen(path, "wb");
	if (!f) { CHECK(!"open ppm"); return; }
	fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
	for (y = 0; y < s->h; y++)
		for (x = 0; x < s->w; x++) {
			const uint8_t *row = s->bits + (uint32_t)y * s->rowBytes;
			if (s->depth == 8) {
				fwrite(pal + row[x] * 3, 1, 3, f);
			} else {
				uint8_t v = (row[x >> 2] >> (6 - 2 * (x & 3))) & 3, g = (uint8_t)(255 - v * 85);
				fputc(g, f); fputc(g, f); fputc(g, f);
			}
		}
	fclose(f);
}

/* A mid-grey background, so the card's edges show. */
static void backdrop(Surface *s)
{
	gfx_fill(s, 0, 0, s->w, s->h, UIC_BAR);
}

/* ---- the rules on their own ---- */

static const uint8_t pars18[18] = { 4, 4, 3, 5, 4, 4, 3, 4, 5, 4, 3, 4, 5, 4, 4, 3, 5, 4 };

static void order_tests(void)
{
	Round r;
	vec2 pin = vec2_make(YD(100), YD(400));
	uint8_t pars[3] = { 4, 3, 5 };

	rules_init(&r, "Par386 Test", 3, 3, pars, 99);
	CHECK(r.players == 3 && r.holes == 3 && r.hole == 0 && !r.over && r.seed == 99);
	CHECK(strcmp(r.pl[0].name, "Player 1") == 0 && strcmp(r.pl[3].name, "Player 4") == 0);
	CHECK(strcmp(r.course, "Par386 Test") == 0);
	CHECK(r.order[0] == 0 && r.order[1] == 1 && r.order[2] == 2);
	/* tee shots in order, whoever's farther or not */
	CHECK(rules_next(&r, pin) == 0);
	r.pl[0].taken = 1; r.pl[0].ball = vec2_make(YD(100), YD(150));
	CHECK(rules_next(&r, pin) == 1);
	r.pl[1].taken = 1; r.pl[1].ball = vec2_make(YD(100), YD(200));
	CHECK(rules_next(&r, pin) == 2);
	/* out of bounds off the tee: back on it with 2 taken, and now the farthest */
	r.pl[2].taken = 2; r.pl[2].ball = vec2_make(YD(100), YD(10));
	CHECK(rules_next(&r, pin) == 2);
	r.pl[2].taken = 3; r.pl[2].ball = vec2_make(YD(100), YD(250));
	CHECK(rules_next(&r, pin) == 0);
	/* the same one again if they're still farthest */
	r.pl[0].taken = 2; r.pl[0].ball = vec2_make(YD(100), YD(190));
	CHECK(rules_next(&r, pin) == 0);
	r.pl[0].ball = vec2_make(YD(100), YD(390));
	CHECK(rules_next(&r, pin) == 1);
	/* across counts as much as along */
	r.pl[1].ball = vec2_make(YD(30), YD(400));
	r.pl[2].ball = vec2_make(YD(100), YD(329));
	CHECK(rules_next(&r, pin) == 2);
	/* a tie goes to the one earlier on the tee */
	r.pl[2].ball = vec2_make(YD(100), YD(330));
	CHECK(rules_next(&r, pin) == 1);
	/* holed players are skipped, -1 once all are */
	rules_hole_out(&r, 1, 5);
	CHECK(r.pl[1].holed && r.pl[1].score[0] == 5);
	CHECK(rules_next(&r, pin) == 2);
	CHECK(!rules_hole_over(&r));
	rules_hole_out(&r, 2, 6);
	rules_hole_out(&r, 0, 4);
	CHECK(rules_hole_over(&r) && rules_next(&r, pin) == -1);

	/* the honour: lowest score on the hole first */
	rules_advance(&r);
	CHECK(r.hole == 1 && !r.over);
	CHECK(r.order[0] == 0 && r.order[1] == 1 && r.order[2] == 2);
	CHECK(r.turn == 0 && r.pl[0].taken == 0 && !r.pl[0].holed && r.pl[2].lie == LIE_TEE);
	rules_hole_out(&r, 0, 4);
	rules_hole_out(&r, 1, 3);
	rules_hole_out(&r, 2, 3);
	rules_advance(&r);
	/* 1 and 2 tie on 3, keeping their order from this tee */
	CHECK(r.order[0] == 1 && r.order[1] == 2 && r.order[2] == 0);
	CHECK(rules_next(&r, pin) == 1);
	rules_hole_out(&r, 0, 5);
	rules_hole_out(&r, 1, 6);
	rules_hole_out(&r, 2, 6);
	rules_advance(&r);
	CHECK(r.over && r.hole == 2);
	/* ties keep the last order: 2 stays after 1 */
	CHECK(r.order[0] == 0 && r.order[1] == 1 && r.order[2] == 2);

	/* totals */
	CHECK(rules_strokes(&r, 0, 0, 3) == 13 && rules_strokes(&r, 1, 0, 3) == 14);
	CHECK(rules_par(&r, 0, 3) == 12 && rules_par(&r, 1, 2) == 3);
	CHECK(rules_to_par(&r, 0) == 1 && rules_to_par(&r, 1) == 2 && rules_to_par(&r, 2) == 3);
	CHECK(rules_played(&r, 0, 0, 3) == 3);
	/* past the course's holes counts nothing */
	CHECK(rules_strokes(&r, 0, 2, 30) == 5 && rules_par(&r, 0, 30) == 12);

	/* players clamped to 1 to 4 */
	rules_init(&r, "x", 0, 3, pars, 1);
	CHECK(r.players == 1);
	rules_init(&r, "x", 9, 3, pars, 1);
	CHECK(r.players == 4);
}

/* An 18 hole round for 4, part played: totals over the nines and against par. */
static void round18(Round *r, uint8_t upto)
{
	uint8_t h, i;
	rules_init(r, "Par386 Auld Kirk", 4, 18, pars18, 7);
	for (h = 0; h < upto; h++) {
		for (i = 0; i < 4; i++) {
			/* par, a birdie on every third hole for player 2, double bogeys for player 4 */
			uint8_t s = pars18[h];
			if (i == 1 && h % 3 == 0) s--;
			if (i == 3) s += 2;
			if (i == 2 && h == 4) s = 12;
			rules_hole_out(r, i, s);
		}
		rules_advance(r);
	}
}

static void total_tests(void)
{
	Round r;

	round18(&r, 18);
	CHECK(r.over);
	CHECK(rules_par(&r, 0, 9) == 36 && rules_par(&r, 9, 18) == 36 && rules_par(&r, 0, 18) == 72);
	CHECK(rules_strokes(&r, 0, 0, 18) == 72 && rules_to_par(&r, 0) == 0);
	CHECK(rules_strokes(&r, 1, 0, 9) == 33 && rules_strokes(&r, 1, 9, 18) == 33 && rules_to_par(&r, 1) == -6);
	CHECK(rules_strokes(&r, 2, 0, 9) == 44 && rules_to_par(&r, 2) == 8);
	CHECK(rules_strokes(&r, 3, 0, 18) == 108 && rules_to_par(&r, 3) == 36);
	/* the honour after 18: player 2 had the last birdie (hole 16 is 15 % 3 == 0) */
	CHECK(r.order[0] == 1);

	round18(&r, 11);
	CHECK(r.hole == 11 && !r.over);
	CHECK(rules_played(&r, 0, 0, 9) == 9 && rules_played(&r, 0, 9, 18) == 2);
	CHECK(rules_strokes(&r, 0, 9, 18) == 7 && rules_to_par(&r, 3) == 22);
}

static void save_tests(void)
{
	Round r, q;
	uint8_t buf[RULES_SAVE_MAX + 8], bad[RULES_SAVE_MAX + 8];
	uint16_t len, old_len;
	uint8_t pars[3] = { 4, 3, 5 };

	/* a round mid-hole, round trip */
	round18(&r, 5);
	r.pl[0].taken = 2; r.pl[0].ball = vec2_make(YD(64.25), YD(190.5)); r.pl[0].lie = LIE_FAIRWAY;
	r.pl[1].taken = 1; r.pl[1].ball = vec2_make(YD(80), YD(220)); r.pl[1].lie = LIE_ROUGH;
	rules_hole_out(&r, 2, 3);
	r.turn = 1;
	memset(r.pl[3].name, 0, sizeof r.pl[3].name);
	strcpy(r.pl[3].name, "Dan");
	len = rules_pack(&r, buf);
	CHECK(len == 2 + 32 + 4 + 9 + 18 + 5 + 4 * (9 + 12 + 18) + RULES_STATS_SAVE(4) + 1);
	CHECK(buf[0] == 0 && buf[1] == RULES_SAVE_VERSION);
	memset(&q, 0x55, sizeof q);
	CHECK(rules_unpack(&q, buf, len) == 0);
	CHECK(memcmp(&q, &r, sizeof r) == 0);

	{
		static const PlayerProfile profile = { "SAM WEST", GOLFER_FEMALE, SKIN_DARK, 1 };
		static const PlayerProfile standard = { PLAYER_DEFAULT_NAME, GOLFER_MALE, SKIN_LIGHT, 0 };
		r.pl[0].cpu = CPU_FAIR;
		CHECK(rules_profile(&r, &profile) == 1);
		CHECK(strcmp(r.pl[1].name, profile.name) == 0 && strcmp(r.pl[3].name, "Dan") == 0);
		len = rules_pack(&r, buf);
		CHECK(rules_unpack(&q, buf, len) == 0 && strcmp(q.pl[1].name, profile.name) == 0);
		CHECK(rules_profile(&q, &standard) == 1 && strcmp(q.pl[1].name, "Player 2") == 0);
	}

	/* a short round for 2 */
	rules_init(&r, "Par386 Test", 2, 3, pars, 0xDEADBEEFUL);
	len = rules_pack(&r, buf);
	CHECK(len == 2 + 32 + 4 + 9 + 3 + 5 + 2 * (9 + 12 + 3) + RULES_STATS_SAVE(2) + 1);
	CHECK(rules_unpack(&q, buf, len) == 0 && memcmp(&q, &r, sizeof r) == 0);
	/* the tee set round trips; a version 2 record (no tee byte) loads from the first */
	r.tee = 2;
	r.holeout = 1;
	len = rules_pack(&r, buf);
	CHECK(buf[53] == 2 && rules_unpack(&q, buf, len) == 0 && q.tee == 2 && q.holeout && memcmp(&q, &r, sizeof r) == 0);
	old_len = (uint16_t)(len - RULES_STATS_SAVE(r.players) - 1);
	{
		uint8_t v2[RULES_SAVE_MAX];
		uint16_t n = 0, i;
		memcpy(v2, buf, len);
		v2[1] = 6;
		CHECK(rules_unpack(&q, v2, len - 1) == 0 && q.span == SPAN_FULL && q.stats_available);
		v2[1] = 5;
		CHECK(rules_unpack(&q, v2, old_len) == 0 && q.holeout && !q.stats_available);
		v2[1] = 3;
		CHECK(rules_unpack(&q, v2, old_len - 1) == 0 && q.tee == 2 && !q.holeout && !q.repeat);
		CHECK(rules_unpack(&q, v2, len) == -1);
		for (i = 0; i < old_len - 1; i++)
			if (i != 53) v2[n++] = buf[i];
		v2[1] = 2;
		CHECK(rules_unpack(&q, v2, n) == 0 && q.tee == 0 && !q.holeout && !q.repeat);
		CHECK(rules_unpack(&q, v2, len) == -1);
		v2[1] = 3;
		CHECK(rules_unpack(&q, v2, n) == -1);
	}
	r.tee = 0;
	r.holeout = 0;
	len = rules_pack(&r, buf);

	/* a version 1 record, before the modes: the same without the mode, the
	 * practice count and each player's computer level, loads as stroke play */
	{
		uint8_t v1[RULES_SAVE_MAX];
		uint16_t n = 0, i;
		r.pl[1].taken = 2; r.pl[1].ball = vec2_make(YD(70), YD(200)); r.pl[1].lie = LIE_ROUGH;
		r.pl[0].score[0] = 0;
		len = rules_pack(&r, buf);
		for (i = 0; i < 50; i++) v1[n++] = buf[i];
		for (i = 54; i < old_len - 1; i++)
			if (i != 54 + 20 && i != 54 + 24 + 20) v1[n++] = buf[i];
		v1[1] = 1;
		CHECK(n == old_len - 5 - 2);
		memset(&q, 0x55, sizeof q);
		CHECK(rules_unpack(&q, v1, n) == 0 && !q.stats_available && q.mode == MODE_STROKE && !q.pl[1].cpu);
		q.stats_available = 1;
		CHECK(memcmp(&q, &r, sizeof r) == 0);
		CHECK(rules_unpack(&q, v1, n - 1) == -1);
		v1[1] = 2;
		CHECK(rules_unpack(&q, v1, n) == -1);
		r.pl[1].taken = 0; r.pl[1].ball = vec2_make(0, 0); r.pl[1].lie = LIE_TEE;
		len = rules_pack(&r, buf);
	}

	/* bad ones */
	CHECK(rules_unpack(&q, buf, len - 1) == -1);
	CHECK(rules_unpack(&q, buf, len + 1) == -1);
	CHECK(rules_unpack(&q, buf, 10) == -1);
	CHECK(rules_unpack(&q, buf, 0) == -1);
#define BAD(off, val) do { memcpy(bad, buf, len); bad[off] = (val); CHECK(rules_unpack(&q, bad, len) == -1); } while (0)
	BAD(1, RULES_SAVE_VERSION + 1);           /* another version */
	BAD(0, 1);
	BAD(2, 0);                                /* no course name */
	BAD(2 + 31, 'x');                         /* no end to it */
	BAD(5, 7);                                /* a control character */
	BAD(38, 3);                               /* players: the length won't match */
	BAD(38, 0);
	BAD(40, 3);                               /* hole past the last */
	BAD(41, 2);                               /* over isn't 0 or 1 */
	BAD(42, 2);                               /* turn past the players */
	BAD(43, 1);                               /* player 1 twice in the order */
	BAD(45, 0);
	BAD(47, 0);                               /* par 0 */
	BAD(50, MODE_COUNT);                      /* a mode past the last */
	BAD(50, MODE_BEST);                       /* best ball for 2 */
	BAD(50, MODE_RANGE);                      /* practice for 2 */
	BAD(53, COURSE_MAX_TEES);                 /* a tee set past the last */
	BAD(old_len - 1, 2);                     /* repeat outside the practice green */
	BAD(old_len - 1, 4);                     /* unknown round option */
	BAD(old_len, 2);                         /* invalid stats availability */
	BAD(old_len + 1, 1);                     /* a fairway without a tee shot */
	BAD(old_len + 4, 2);                     /* invalid green marker */
	BAD(54 + 9 + 8, LIE_COUNT);               /* a lie past the last */
	BAD(54 + 9 + 8 + 2, 2);                   /* holed isn't 0 or 1 */
	BAD(54 + 9 + 8 + 3, CPU_LEVELS);          /* a computer level past the last */
	BAD(54 + 8, 'x');                         /* a name with no end */
	BAD(54 + 9, 0x7F);                        /* a ball far off the hole */
	BAD(54 + 9 + 8 + 2, 1);                   /* the turn is a holed player's */
	BAD(54 + 9 + 12 + 1, 4);                  /* strokes on a hole not reached */
	memcpy(bad, buf, len);                    /* match play for 2, player 2 a computer golfer: fine */
	bad[50] = MODE_MATCH; bad[54 + 24 + 20] = CPU_PRO;
	CHECK(rules_unpack(&q, bad, len) == 0 && q.mode == MODE_MATCH && q.pl[1].cpu == CPU_PRO);
	memcpy(bad, buf, len);                    /* player 1 holed, player 2 to play: fine */
	bad[42] = 1; bad[54 + 19] = 1;
	CHECK(rules_unpack(&q, bad, len) == 0);
	bad[54 + 24 + 19] = 1;                    /* everyone holed but the hole not over */
	CHECK(rules_unpack(&q, bad, len) == -1);
	bad[41] = 1;                              /* over but the last hole's players still holed */
	CHECK(rules_unpack(&q, bad, len) == -1);
#undef BAD
	/* a good one again after all that */
	CHECK(rules_unpack(&q, buf, len) == 0);
}

/* ---- a round through play.c ---- */

typedef struct {
	uint8_t *rec;
	uint32_t len;
	Hole h;
	SurfaceGrid g;
	uint8_t *cells;
} TestHole;

static TestHole holes[COURSE_MAX_HOLES];
static TestHole *filling;

static void put_row(void *ctx, uint16_t row, const uint8_t *bytes, uint16_t len)
{
	(void)ctx;
	memcpy(filling->cells + (uint32_t)row * filling->g.stride, bytes, len);
}

static int load_course(const char *path, Course *c)
{
	uint32_t n, len;
	uint8_t *pdb = load_file(path, &n), *rec;
	uint8_t row[SURF_ROW_MAX];
	int k;

	if (!pdb) return 0;
	rec = pdb_record(pdb, n, 0, &len);
	if (!rec || course_parse(c, rec, len) != HOLE_OK) return 0;
	free(rec);
	for (k = 0; k < c->hole_count; k++) {
		TestHole *t = &holes[k];
		t->rec = pdb_record(pdb, n, k + 1, &t->len);
		if (!t->rec || hole_parse(&t->h, t->rec, t->len) != HOLE_OK) return 0;
		surface_size(&t->h, &t->g);
		t->cells = malloc((size_t)t->g.stride * t->g.h);
		filling = t;
		surface_raster(&t->h, row, put_row, NULL);
		t->g.cells = t->cells;
	}
	free(pdb);
	return 1;
}

/* play_next_view during each result against the scene asked for after it:
 * views drawn ahead, ones that matched, and scenes asked for with none ahead. */
static int ahead_n, ahead_ok, ahead_none, ahead_fly;
/* the view play_next_view gave during the flight, to match the result's */
static int fly_have;
static RenderView fly_v;

/* Frames from t until the state changes or the view wants drawing again,
 * as the platform's loop; a scene drawn ahead during a result has to be
 * the one asked for next. */
static int run(Play *p, Surface *s, Surface *bg, uint32_t *t, uint8_t until_not)
{
	int f = 0, k, have = 0;
	RenderView v;
	for (k = 0; k < 4000 && p->state == until_not; k++) {
		uint8_t st = p->state;
		if (st == PS_FLY && !have) {
			fly_have = have = play_next_view(p, &fly_v) ? 1 : -1;
		}
		if (st == PS_RESULT && !have) {
			have = play_next_view(p, &v) ? 1 : -1;
			if (have > 0) ahead_n++;
			if (fly_have) {
				CHECK(fly_have == have);
				if (fly_have > 0 && have > 0)
					CHECK(fly_v.hole == v.hole && fly_v.grid == v.grid && fly_v.ball.x == v.ball.x && fly_v.ball.y == v.ball.y &&
					      fly_v.aim == v.aim && fly_v.pin == v.pin && fly_v.green == v.green && fly_v.player == v.player);
				ahead_fly++;
				fly_have = 0;
			}
		}
		*t += 20;
		f = play_frame(p, s, bg, *t);
		if (st == PS_RESULT && have && (f & PL_RENDER)) {
			if (have > 0) {
				int same = p->rv.hole == v.hole && p->rv.grid == v.grid && p->rv.ball.x == v.ball.x && p->rv.ball.y == v.ball.y &&
				           p->rv.aim == v.aim && p->rv.pin == v.pin && p->rv.green == v.green && p->rv.player == v.player;
				CHECK(same);
				ahead_ok += same;
			} else if (!p->cpu) {
				ahead_none++;
			}
		}
		if (st == PS_RESULT && have > 0 && p->state != PS_RESULT && !(f & PL_RENDER)) CHECK(!"drawn ahead for no scene");
		if (f & (PL_RENDER | PL_CARD | PL_BACK)) break;
	}
	return f;
}

/* One shot for whoever's turn it is: 95 % with the snap on time, or a putt
 * at the meter's cup tick. Returns the flags at the end. */
/* The Frond replays of this round (tools/replay/round-a.replay and
 * round-b.replay): the same taps at the same times from the meter's start,
 * with waits for the IIIx to draw each view and play each shot out. rt is
 * the replay's clock in ms. */
static FILE *rf;
static uint32_t rt;

#define IIIX_SCENE_MS 4500   /* a scene and the turn strip, with room to spare */
#define IIIX_HOLE_MS  2500   /* loading a hole's surface grid */
#define IIIX_CLOCK_MS 120    /* the meter's clock starts this long after the press, once its first frames show */

static void ev(uint32_t at, const char *what)
{
	if (rf) fprintf(rf, "%lu %s\n", (unsigned long)at, what);
}

static void tap_at(uint32_t at, int x, int y)
{
	char b[40];
	snprintf(b, sizeof b, "pen-down %d %d", x, y);
	ev(at, b);
	ev(at + 60, "pen-up");
}

static void snap(uint32_t at, const char *name)
{
	char b[80];
	snprintf(b, sizeof b, "snapshot \"{out}/%s.png\"", name);
	ev(at, b);
}

static uint32_t ten(double ms) { return (uint32_t)(ms / 10 + 0.5) * 10; }

static int shot(Play *p, Surface *s, Surface *bg, uint32_t *t)
{
	int f;
	uint32_t last;
	*t += 1000;
	play_frame(p, s, bg, *t);
	CHECK(p->state == PS_AIM);
	CHECK(play_key(p, KEY_SELECT, *t) & PL_ANIM);
	tap_at(rt, 20, 150);
	rt += IIIX_CLOCK_MS;
	if (p->club == CLUB_PT) {
		fixed d = vec2_len(vec2_sub(p->rv.hole->pins[0], p->rv.ball));
		uint32_t ms = ten((double)d / p->range * MTR_UP_MS);
		*t += ms;
		play_tap(p, 80, 60, *t);
		last = rt + ms;
	} else {
		/* less than 95 % when the club's carry is past the pin, snap on time */
		double d = vec2_len(vec2_sub(p->rv.hole->pins[0], p->rv.ball)) / 65536.0;
		uint32_t up = 950, c = ball_carry(p->club, p->lie), down;
		if (d < c * 0.95) up = ten(1000 * d / c);
		if (up < 200) up = 200;
		down = ten(up * (double)MTR_DOWN_MS / MTR_UP_MS);
		*t += up;
		play_tap(p, 80, 60, *t);
		tap_at(rt + up, 80, 60);
		*t += down;
		play_key(p, KEY_SELECT, *t);
		last = rt + up + down;
	}
	tap_at(last, 80, 60);
	CHECK(p->state == PS_FLY);
	/* the physics, the flight and roll as play.c plays them back, then the result */
	rt = last + 600 + (uint32_t)p->shot.fly_n * 42 + (uint32_t)(p->shot.n - p->shot.fly_n) * 62 + 1500 + 1000;
	f = run(p, s, bg, t, PS_FLY);
	f = run(p, s, bg, t, PS_RESULT);
	if (f & PL_RENDER) rt += IIIX_SCENE_MS;
	return f;
}

/* The farthest of the players not holed, or the first in the tee order yet to tee off. */
static int expect_next(const Round *r, vec2 pin)
{
	int k, best = -1;
	double far = -1;
	for (k = 0; k < r->players; k++)
		if (!r->pl[r->order[k]].holed && !r->pl[r->order[k]].taken) return r->order[k];
	for (k = 0; k < r->players; k++) {
		const RoundPlayer *q = &r->pl[r->order[k]];
		double dx = (q->ball.x - pin.x) / 65536.0, dy = (q->ball.y - pin.y) / 65536.0, d = dx * dx + dy * dy;
		if (q->holed) continue;
		if (d > far + 1e-9) { far = d; best = r->order[k]; }
	}
	return best;
}

/* A round saved after a shot and picked up again: the player who hit
 * doesn't play again unless they're still the farthest. */
static void resume_tests(const Course *c)
{
	static Play p;
	Round r, q;
	uint8_t buf[RULES_SAVE_MAX];
	uint16_t len;
	vec2 pin;

	rules_init(&r, "Par386 Test", 2, c->hole_count, c->par, 3);
	play_start(&p, &holes[0].h, &holes[0].g, &r);
	pin = p.rv.hole->pins[0];
	/* player 1 teed off; the shot was settled but the turn is still theirs */
	r.pl[0].taken = 1; r.pl[0].lie = LIE_FAIRWAY; r.pl[0].ball = vec2_make(YD(40), YD(150));
	r.turn = 0;
	play_settle(&p);
	CHECK(r.turn == 1);
	r.turn = 0;
	len = rules_pack(&r, buf);
	CHECK(rules_unpack(&q, buf, len) == 0);
	play_start(&p, &holes[0].h, &holes[0].g, &q);
	CHECK(p.cur == 1 && q.turn == 1 && p.stroke == 1 && p.lie == LIE_TEE);

	/* a later shot: player 2 is on the fairway, nearer, so player 1 plays again */
	q.pl[1].taken = 1; q.pl[1].lie = LIE_FAIRWAY; q.pl[1].ball = vec2_make(YD(40), YD(250));
	q.turn = 1;
	len = rules_pack(&q, buf);
	CHECK(rules_unpack(&r, buf, len) == 0);
	play_start(&p, &holes[0].h, &holes[0].g, &r);
	CHECK(p.cur == expect_next(&r, pin) && p.cur == 0 && p.stroke == 2 && r.turn == 0);
	/* and the other way round, player 2 farther */
	r.pl[0].ball = vec2_make(YD(40), YD(300));
	r.turn = 0;
	play_start(&p, &holes[0].h, &holes[0].g, &r);
	CHECK(p.cur == 1 && r.turn == 1);
}

static void mode_tests(const Course *c);

static uint8_t pixel(const Surface *s, int16_t x, int16_t y)
{
	const uint8_t *row = s->bits + (uint32_t)y * s->rowBytes;
	return s->depth == 8 ? row[x] : (row[x >> 2] >> (6 - 2 * (x & 3))) & 3;
}

static uint8_t map_pixel(const Surface *s, int16_t x, int16_t y)
{
	return pixel(s, x * s->scale, y * s->scale);
}

static void map_aim_tests(const Course *c, Surface *s)
{
	static Play p;
	const Hole *h = &holes[0].h;
	const SurfaceGrid *g = &holes[0].g;
	Round r;
	angle aim;
	fixed distance[2];
	uint8_t i;

	rules_init(&r, "Par386 Test", 1, c->hole_count, c->par, 3);
	play_start(&p, h, g, &r);
	p.wind_mph = 13;
	CHECK(play_key(&p, KEY_MAP, 0) == PL_MAP);
	play_map(s, &p);
	p.wind_dir = 0x4000;
	aim = p.aim;
	p.aim = 0;
	play_status(s, &p, NULL);
	CHECK(map_pixel(s, 145, 140) == s->pa[UIC_TEXT_HI] && map_pixel(s, 149, 144) != s->pa[UIC_TEXT_HI]);
	p.wind_dir = 0;
	p.aim = aim;
	play_status(s, &p, NULL);
	CHECK(map_pixel(s, 145, 140) != s->pa[UIC_TEXT_HI] && map_pixel(s, 149, 144) == s->pa[UIC_TEXT_HI]);
	CHECK(play_tap(&p, 1, 1, 0) == 0 && p.view == PLAY_MAP);
	CHECK(surface_at(g, YD(135), YD(235)) == SURF_WATER);
	for (i = 0; i < 2; i++) {
		vec2 to = vec2_make(i ? YD(145) : YD(131), i ? YD(260) : YD(216));
		int16_t x = (int16_t)((p.mg.left + to.x / p.mg.ypp) / s->scale);
		int16_t y = (int16_t)((p.mg.top + (p.mg.length - to.y) / p.mg.ypp) / s->scale);
		CHECK(play_tap(&p, x, y, 0) == PL_MAP && p.view == PLAY_MAP);
		CHECK(fix_abs(p.target.x - to.x) < YD(4) && fix_abs(p.target.y - to.y) < YD(4));
		distance[i] = vec2_len(vec2_sub(p.target, p.rv.ball));
		CHECK(p.target_range == distance[i] && p.aim == vec2_angle(vec2_sub(p.target, p.rv.ball)));
		play_map(s, &p);
		play_status(s, &p, NULL);
		save(s, i ? "map-aim-carry" : "map-aim-layup");
	}
	CHECK(distance[0] < YD(225) && distance[1] > YD(250));
	aim = p.aim;
	{
		HazardCarry hit[3];
		CHECK(hole_hazards(h, g, p.rv.ball, aim, hit, 3) > 0);
		CHECK(hit[0].surface == SURF_WATER && hit[0].entry > 200 && hit[0].clear <= 264 &&
		      hit[0].entry < hit[0].clear);
		CHECK(play_tap(&p, 20, 155, 0) == PL_CARD && p.card == CARD_BOOK);
		play_card(s, &p);
		save(s, "yardage-book");
		CHECK(play_tap(&p, 80, 97, 0) == PL_MAP && p.aim == aim);
		CHECK(play_key(&p, KEY_UP, 0) == PL_CARD && p.card == CARD_BOOK);
		CHECK(play_key(&p, KEY_BACK, 0) == PL_MAP && p.aim == aim && p.target_range == distance[1]);
	}
	CHECK(play_key(&p, KEY_LEFT, 0) == PL_MAP && p.aim == (angle)(aim + 910));
	CHECK(p.target_range == distance[1] && abs((int16_t)(vec2_angle(vec2_sub(p.target, p.rv.ball)) - p.aim)) <= 2);
	CHECK(play_key(&p, KEY_RIGHT, 0) == PL_MAP && p.aim == aim);
	CHECK(play_tap(&p, 97, 155, 0) == 0 && p.view == PLAY_MAP);
	CHECK(play_tap(&p, 98, 155, 0) == PL_CARD && p.view == PLAY_CARD);
	CHECK(play_key(&p, KEY_SELECT, 0) == PL_MAP && p.view == PLAY_MAP && p.aim == aim);
	CHECK(play_tap(&p, 96, 155, 0) == PL_SCENE && p.view == PLAY_SCENE && p.aim == aim);
	CHECK(play_key(&p, KEY_MAP, 0) == PL_MAP && p.target_range == distance[1] && p.aim == aim);
	CHECK(play_key(&p, KEY_MAP, 0) == PL_SCENE && p.view == PLAY_SCENE);
	CHECK(p.target_set && play_key(&p, KEY_DOWN, 0) == 0 && p.target_set && p.target_range == distance[1]);
	CHECK(play_key(&p, KEY_LEFT, 0) == 0 && !p.target_set && !p.target_range);

	/* From the rough, put the ball, bunker and flag in the same close view. */
	{
		RenderMap whole, near;
		vec2 target = h->pins[0];
		int16_t x, y, sc = s->scale;

		r.pl[0].ball = vec2_make(YD(110), YD(382));
		r.pl[0].lie = LIE_ROUGH;
		r.pl[0].taken = 1;
		play_start(&p, h, g, &r);
		CHECK(play_key(&p, KEY_MAP, 0) == PL_MAP);
		play_map(s, &p);
		x = (int16_t)((p.mg.left + target.x / p.mg.ypp) / sc);
		y = (int16_t)((p.mg.top + (p.mg.length - target.y) / p.mg.ypp) / sc);
		CHECK(play_tap(&p, x, y, 0) == PL_MAP);
		play_map(s, &p);
		play_status(s, &p, NULL);
		save(s, "greenside-hole");
		whole = p.mg;
		target = p.target;
		aim = p.aim;
		CHECK(play_tap(&p, 20, 125, 0) == PL_MAP && p.map_near);
		CHECK(p.target.x == target.x && p.target.y == target.y && p.aim == aim);
		play_map(s, &p);
		play_status(s, &p, NULL);
		save(s, "greenside-near");
		near = p.mg;
		CHECK(near.ypp < whole.ypp && near.bx >= 0 && near.bx < s->w && near.px < s->w &&
		      near.py >= 0 && near.by < (s->lh - RENDER_STATUS) * sc);
		x = (int16_t)((near.left + YD(128) / near.ypp) / sc);
		y = (int16_t)((near.top + (near.length - YD(397)) / near.ypp) / sc);
		CHECK(play_tap(&p, x, y, 0) == PL_MAP && p.target_set);
		CHECK(fix_abs(p.target.x - YD(128)) <= near.ypp * sc &&
		      fix_abs(p.target.y - YD(397)) <= near.ypp * sc);
		play_map(s, &p);
		CHECK(p.mg.ypp == near.ypp && p.mg.left == near.left && p.mg.top == near.top);
		target = p.target;
		aim = p.aim;
		CHECK(play_tap(&p, 116, 155, 0) == PL_CARD && p.card == CARD_VIEW);
		CHECK(play_key(&p, KEY_SELECT, 0) == PL_MAP && p.map_near && p.aim == aim);
		play_map(s, &p);
		CHECK(play_key(&p, KEY_DOWN, 0) == PL_MAP && !p.map_near);
		play_map(s, &p);
		CHECK(p.mg.ypp == whole.ypp && p.target.x == target.x && p.target.y == target.y);
		CHECK(play_tap(&p, 81, 155, 0) == PL_SCENE && p.aim == aim);
	}
}

static void book_edge_test(void)
{
	static const uint8_t cells[] = { SURF_ROUGH << 4, SURF_BUNKER << 4, SURF_ROUGH << 4,
				       SURF_WATER << 4, SURF_WATER << 4 };
	Hole h = {0};
	SurfaceGrid g;
	HazardCarry hit[3];
	vec2 ball = vec2_make(YD(1), YD(1));

	h.width = 2;
	h.length = 10;
	surface_size(&h, &g);
	g.cells = cells;
	CHECK(hole_hazards(&h, &g, ball, ANG_90, hit, 3) == 2);
	CHECK(hit[0].surface == SURF_BUNKER && hit[0].entry == 1 && hit[0].clear == 3);
	CHECK(hit[1].surface == SURF_WATER && hit[1].entry == 5 && !hit[1].clear);
	CHECK(hole_hazards(&h, &g, ball, ANG_90, hit, 1) == 1);
}

/* The ball, trail and contact mark clear with or without a kept scene. */
static void flight_scene(const Play *shot, Surface *bg)
{
	static Play kept, lean;
	Surface a, b;
	uint32_t t;
	uint8_t seen = 0;

	kept = lean = *shot;
	surface(&a, bg->scale);
	surface(&b, bg->scale);
	gfx_copy(&a, bg, 0, 0, bg->w, bg->h);
	gfx_copy(&b, bg, 0, 0, bg->w, bg->h);
	CHECK(kept.shot.land_pt > 0 && kept.shot.land_pt < kept.shot.fly_n);
	for (t = 1240; kept.state == PS_FLY && t < 10000; t += 42) {
		uint8_t i, landed = 0;
		play_frame(&kept, &a, bg, t);
		play_frame(&lean, &b, NULL, t);
		CHECK(memcmp(a.bits, b.bits, (size_t)a.rowBytes * a.h) == 0);
		for (i = 0; i < kept.npatch; i++)
			if (kept.patch[i].w == 7 * a.scale && kept.patch[i].h == 7 * a.scale) landed = 1;
		CHECK(landed == (kept.shown >= kept.shot.land_pt));
		if (t == 1786) save(&a, "scene-ball-flight");
		if (landed && !seen) {
			save(&a, "scene-first-contact");
			seen = 1;
		}
	}
	CHECK(seen && kept.state == PS_RESULT);
	CHECK(strncmp(kept.msg1, "Carry ", 6) == 0 && strstr(kept.msg1, " Total "));
	CHECK(strstr(kept.msg2, " Pin "));
	save(&a, "scene-flight-result");
	free(a.bits);
	free(b.bits);
}

static void pen_tests(const Course *c, Surface *s)
{
	static Play p;
	const Hole *h = &holes[0].h;
	const SurfaceGrid *g = &holes[0].g;
	RenderArt art = {0};
	RenderGolfer golfer = {0};
	Surface bg;
	Round r;
	char file[32];
	void *work = malloc(render_work_size(s));
	uint8_t club, landed = 0;
	uint32_t t;

	surface(&bg, s->scale);
	snprintf(file, sizeof file, "Bkd8-%d.bin", 999 + s->scale);
	art.backdrop.p = load_art(file, &art.backdrop.len);
	snprintf(file, sizeof file, "Spr8-%d.bin", 1999 + s->scale);
	golfer.pose[0].p = load_art(file, &golfer.pose[0].len);
	golfer.under = malloc(RENDER_UNDER_UNITS * RENDER_UNDER_UNITS * 4);
	art.golfer = &golfer;
	rules_init(&r, "Par386 Test", 1, c->hole_count, c->par, 3);
	play_start(&p, h, g, &r);
	club = p.club;
	CHECK(render_scene(s, &p.rv, &art, work, NULL) == 0);
	gfx_copy(&bg, s, 0, 0, s->w, s->h);
	play_frame(&p, s, &bg, 0);
	save(s, "pen-swing");
	CHECK(play_tap(&p, 113, 140, 10) == 0 && p.state == PS_AIM);
	CHECK(play_tap(&p, 58, 152, 10) == 0 && p.state == PS_AIM);
	CHECK(play_tap(&p, 144, 152, 10) == 0 && p.state == PS_AIM);
	CHECK(play_tap(&p, 20, 140, 10) == PL_CARD && p.card == CARD_CLUB);
	play_card(s, &p);
	save(s, "pen-clubs");
	CHECK(play_key(&p, KEY_RIGHT, 20) == PL_CARD && p.csel == club + 1);
	CHECK(play_key(&p, KEY_BACK, 20) == PL_SCENE && p.club == club);
	CHECK(play_tap(&p, 20, 140, 20) == PL_CARD);
	CHECK(play_tap(&p, 100, 70, 20) == PL_SCENE && p.club == CLUB_7I);
	CHECK(play_tap(&p, 20, 140, 20) == PL_CARD);
	CHECK(play_tap(&p, 100, 118, 20) == PL_SCENE && p.club == CLUB_7I);
	gfx_copy(s, &bg, 0, 0, s->w, s->h);
	play_frame(&p, s, &bg, 20);
	save(s, "panel-address");
	CHECK(play_tap(&p, 20, 152, 100) == PL_ANIM && p.state == PS_UP);
	play_frame(&p, s, &bg, 100);
	play_shown(&p, 100);
	play_frame(&p, s, &bg, 600);
	save(s, "panel-meter");
	CHECK(play_tap(&p, 110, 141, 750) == PL_ANIM && p.state == PS_DOWN);
	CHECK(play_tap(&p, 120, 60, 1238) == PL_ANIM && p.state == PS_FLY);
	flight_scene(&p, &bg);
	play_frame(&p, s, &bg, 1240);
	save(s, "panel-flight");
	/* The flight map's rose uses the map axes even when the shot aimed across them. */
	p.wind_mph = 13;
	p.wind_dir = 0x4000;
	p.aim = 0;
	p.onmap = 1;
	p.view = PLAY_MAP;
	play_result_pending(&p, 1);
	play_map(s, &p);
	for (t = 2200; t < 20000 && p.state == PS_FLY; t += 40) {
		play_frame(&p, s, NULL, t);
		if (!landed && p.shown >= p.shot.land_pt) {
			save(s, "map-first-contact");
			landed = 1;
		}
		if (t == 2200) {
			play_status(s, &p, NULL);
			CHECK(map_pixel(s, 145, 140) == s->pa[UIC_TEXT_HI] && map_pixel(s, 149, 144) != s->pa[UIC_TEXT_HI]);
			save(s, "panel-map-flight");
		}
	}
	CHECK(p.state == PS_RESULT);
	{
		uint8_t before = map_pixel(s, 100, 123);
		CHECK(play_frame(&p, s, NULL, t) == PL_ANIM);
		play_result_pending(&p, 0);
		CHECK(play_frame(&p, s, NULL, t) == (PL_ANIM | PL_DIRTY));
		CHECK(map_pixel(s, 100, 123) == s->pa[UIC_BRASS_LT]);
		CHECK(before != map_pixel(s, 100, 123));
	}
	save(s, "panel-result");
	{
		vec2 target = vec2_make(YD(145), YD(260));
		int16_t x, y, u = s->scale;
		fixed depth;
		int f;
		uint8_t ink = s->pa[s->depth >= 8 ? UIC_TEXT_HI : UIC_WHITE];

		CHECK(play_frame(&p, s, NULL, t + 2000) == PL_RENDER);
		CHECK(p.have_shot && !p.target_set && play_key(&p, KEY_MAP, t) == PL_MAP);
		play_map(s, &p);
		x = (int16_t)((p.mg.left + target.x / p.mg.ypp) / u);
		y = (int16_t)((p.mg.top + (p.mg.length - target.y) / p.mg.ypp) / u);
		CHECK(play_tap(&p, x, y, t) == PL_MAP && p.target_set);
		target = p.target;
		play_map(s, &p);
		play_status(s, &p, NULL);
		save(s, "map-target-history");
		CHECK(play_key(&p, KEY_DOWN, t) == PL_MAP && p.map_near);
		play_map(s, &p);
		play_status(s, &p, NULL);
		save(s, "map-target-near");
		CHECK(play_tap(&p, 81, 155, t) == PL_SCENE);
		CHECK(p.target_set && p.target.x == target.x && p.target.y == target.y);
		CHECK(render_scene(s, &p.rv, &art, work, NULL) == 0);
		gfx_copy(&bg, s, 0, 0, s->w, s->h);
		f = play_frame(&p, s, &bg, t);
		if (f == PL_RENDER) {
			CHECK(render_scene(s, &p.rv, &art, work, NULL) == 0);
			gfx_copy(&bg, s, 0, 0, s->w, s->h);
			f = play_frame(&p, s, &bg, t);
		}
		CHECK(f & PL_DIRTY);
		CHECK(render_point(&p.cam, target.x, target.y, hole_height(h, target.x, target.y), &x, &y, &depth));
		CHECK(x >= 3 * u && x <= s->w - 4 * u && y >= 3 * u && y <= 130 * u);
		CHECK(pixel(s, x, y) == ink && pixel(s, x - 2 * u, y) == ink &&
			pixel(s, x - u, y) == s->pa[UIC_BLACK]);
		save(s, "map-target-scene");
	}
	p.onmap = 0;
	r.pl[0].ball = vec2_make(h->pins[0].x, h->pins[0].y - YD(7));
	r.pl[0].taken = 2;
	r.pl[0].lie = LIE_GREEN;
	play_start(&p, h, g, &r);
	CHECK(render_green(s, &p.rv) == 0);
	gfx_copy(&bg, s, 0, 0, s->w, s->h);
	play_frame(&p, s, &bg, 2000);
	save(s, "pen-putt");
	CHECK(play_tap(&p, 20, 140, 2010) == 0 && p.state == PS_AIM && p.club == CLUB_PT);
	CHECK(play_tap(&p, 144, 152, 2020) == PL_MAP && p.view == PLAY_MAP);
	CHECK(play_key(&p, KEY_MAP, 2020) == PL_SCENE && p.view == PLAY_SCENE);
	CHECK(play_tap(&p, 20, 152, 2100) == PL_ANIM && p.state == PS_UP);
	CHECK(play_tap(&p, 130, 80, 2700) == PL_ANIM && p.state == PS_FLY);
	/* A tap on Next hole opens the normal scorecard before the next tee. */
	r.pl[0].ball.y = h->pins[0].y - YD(1);
	play_start(&p, h, g, &r);
	CHECK(render_green(s, &p.rv) == 0);
	gfx_copy(&bg, s, 0, 0, s->w, s->h);
	play_frame(&p, s, &bg, 3000);
	CHECK(play_tap(&p, 20, 152, 3100) == PL_ANIM);
	play_frame(&p, s, &bg, 3100);
	play_shown(&p, 3100);
	t = 3100 + (uint32_t)p.putt_tick * MTR_UP_MS / (MTR_W - MTR_SNAP);
	CHECK(play_tap(&p, 130, 80, t) == PL_ANIM && p.state == PS_FLY);
	for (; t < 20000 && p.state != PS_DONE; t += 40) play_frame(&p, s, &bg, t);
	CHECK(p.state == PS_DONE && p.shot.result == SHOT_HOLED);
	CHECK(map_pixel(s, 100, 123) == s->pa[UIC_BRASS_LT]);
	save(s, "panel-next-hole");
	CHECK(play_tap(&p, 115, 128, t) == PL_CARD && p.card == CARD_HOLE);
	free((void *)art.backdrop.p);
	free((void *)golfer.pose[0].p);
	free(golfer.under);
	free(bg.bits);
	free(work);
}

static void round_tests(const char *pdb)
{
	static Play p, q;
	Round r, saved;
	Course c;
	Surface s, bg, s2;
	uint32_t t = 0;
	uint8_t buf[RULES_SAVE_MAX];
	uint16_t len;
	int f = 0, shots = 0, turns = 0, resumed = 0, settled = 0, k;
	uint8_t hole = 0, shot_strip = 0, turn_strip = 0, honour = 0;

	if (!load_course(pdb, &c)) { CHECK(!"load the test course"); return; }
	CHECK(c.hole_count == 3 && c.par[0] == 4 && c.par[1] == 3 && c.par[2] == 5);
	CHECK(holes[1].h.number == 2 && holes[2].h.number == 3 && holes[2].h.length == 500);
	surface(&s, 1);
	surface(&bg, 1);
	surface(&s2, 2);
	map_aim_tests(&c, &s);
	map_aim_tests(&c, &s2);
	pen_tests(&c, &s);
	pen_tests(&c, &s2);

	resume_tests(&c);
	mode_tests(&c);

	/* an Easy golfer 100 yd out with water from 20 to 90 yd: carries it with a
	 * short iron, never a wood at the lay-up's power */
	{
		static uint8_t cells[220 * 50];
		SurfaceGrid wg;
		CpuView v;
		CpuShot o;
		ShotIn in;
		static Shot sh;
		int sd, wet = 0;
		for (k = 0; k < 220; k++) memset(cells + k * 50, k >= 60 && k < 95 ? 0x77 : 0x22, 50);
		wg.cells = cells;
		wg.w = 100;
		wg.h = 220;
		wg.stride = 50;
		memset(&v, 0, sizeof v);
		v.hole = &holes[0].h;
		v.grid = &wg;
		v.ball = vec2_make(YD(100), YD(100));
		v.pin = v.to = vec2_make(YD(100), YD(200));
		v.lie = LIE_FAIRWAY;
		for (sd = 1; sd <= 40; sd++) {
			cpu_shot(&v, CPU_EASY, (uint32_t)sd * 7919, &o);
			CHECK(o.club >= CLUB_7I && o.club <= CLUB_PW);
			memset(&in, 0, sizeof in);
			in.hole = &holes[0].h;
			in.grid = &wg;
			in.pos = v.ball;
			in.pin = v.pin;
			in.aim = o.aim;
			in.power = o.power;
			in.snap = o.snap;
			in.club = o.club;
			in.shape = o.shape;
			in.lie = LIE_FAIRWAY;
			ball_shot(&in, &sh);
			if (sh.result == SHOT_WATER) wet++;
		}
		printf("cpu: Easy over water 20 to 90 yd from 100 yd out, %d of 40 wet\n", wet);
		CHECK(wet <= 8);
	}

	/* a tee set: the hole's own tee, or the nearest longer one; play starts there */
	hole_use_tee(&holes[0].h, 2);
	CHECK(holes[0].h.tees[holes[0].h.tee].set == 2 && holes[0].h.tees[holes[0].h.tee].pos.y == YD(40));
	hole_use_tee(&holes[0].h, 3);
	CHECK(holes[0].h.tees[holes[0].h.tee].set == 2);
	rules_init(&r, "Par386 Test", 1, c.hole_count, c.par, 3);
	r.tee = 2;
	play_start(&p, &holes[0].h, &holes[0].g, &r);
	CHECK(p.rv.ball.y == YD(40) && p.lie == LIE_TEE && p.rv.aim == vec2_angle(vec2_sub(holes[0].h.aim, p.rv.ball)));
	{
		Hole one = holes[0].h;
		one.tee_count = 1;
		one.tees[0].set = 1;
		hole_use_tee(&one, 0);
		CHECK(one.tee == 0);
	}
	hole_use_tee(&holes[0].h, 0);
	CHECK(holes[0].h.tee == 0);

	rules_init(&r, "Par386 Test", 2, c.hole_count, c.par, 3);
	play_start(&p, &holes[0].h, &holes[0].g, &r);
	{
		char path[512];
		snprintf(path, sizeof path, "%s/round-a.replay", outdir);
		rf = fopen(path, "w");
		if (rf) fprintf(rf, "# Made by rulestest: 2 players on the test course for a debug PRC built with\n"
		                    "# EXTRA=-DPLAY_SEED=3, from the first tee once it's drawn, to hole 2 after both tee shots.\n");
		rt = 1000;
	}
	CHECK(p.cur == 0 && p.stroke == 1 && p.lie == LIE_TEE && p.state == PS_AIM && p.view == PLAY_SCENE);
	play_frame(&p, &s, &bg, t);

	while (!r.over && shots < 200) {
		vec2 pin = p.rv.hole->pins[0];
		uint8_t who = p.cur, before = r.pl[who].taken;
		/* whose turn it is, by the rules worked out here */
		CHECK(p.cur == expect_next(&r, pin));
		CHECK(r.turn == p.cur);
		if (!shot_strip && r.hole == 0 && r.pl[0].taken == 1 && p.cur == 1) {
			/* the strip with player 2's name and the lie, at both sizes */
			p.redraw = 8;
			gfx_fill(&s, 0, 0, 160, 160, UIC_BAR);
			play_frame(&p, &s, &bg, t);
			save(&s, "strip-turn");
			play_turn(&s, &p);
			save(&s, "strip-next");
			gfx_fill(&s2, 0, 0, 320, 320, UIC_BAR);
			play_turn(&s2, &p);
			save(&s2, "strip-next");
			shot_strip = 1;
		}
		if (shots < 3 || (shots == 3 && rf)) {
			char b[24];
			snprintf(b, sizeof b, "a-shot%d-aim", shots + 1);
			snap(rt - 300, b);
		}
		f = shot(&p, &s, &bg, &t);
		shots++;
		CHECK(r.pl[who].taken > before || r.pl[who].holed);

		/* half way through hole 2: save, start again from the record and carry on with that */
		if (!resumed && r.hole == 1 && r.pl[0].taken && r.pl[1].taken && p.state != PS_DONE) {
			len = rules_pack(&r, buf);
			memset(&saved, 0, sizeof saved);
			CHECK(rules_unpack(&saved, buf, len) == 0);
			q = p;
			play_start(&p, &holes[saved.hole].h, &holes[saved.hole].g, &saved);
			CHECK(p.cur == q.cur && p.rv.ball.x == q.rv.ball.x && p.rv.ball.y == q.rv.ball.y);
			CHECK(p.lie == q.lie && p.stroke == q.stroke && p.club == q.club && p.state == PS_AIM);
			CHECK(p.wind_mph == q.wind_mph && p.wind_dir == q.wind_dir);
			r = saved;
			p.round = &r;
			f = PL_RENDER;
			resumed = 1;
			/* the app is left here: the second replay starts on the scene after Continue round */
			snap(rt - 300, "a-leave");
			if (rf) fclose(rf);
			{
				char path[512];
				snprintf(path, sizeof path, "%s/round-b.replay", outdir);
				rf = fopen(path, "w");
				if (rf) fprintf(rf, "# Made by rulestest: carries on from round-a.replay after the app is left and\n"
				                    "# Continue round is picked, from the scene once it's drawn, to the clubhouse.\n");
				rt = 1000;
				snap(500, "b-resume");
			}
		}

		if (p.state == PS_DONE) {
			/* the strip shows the score until a tap */
			CHECK(strstr(p.msg1, "shot") != NULL && strncmp(p.msg1, r.pl[p.cur].name, strlen(r.pl[p.cur].name)) == 0);
			CHECK(strlen(p.msg1) <= 20 && strlen(p.msg2) <= 20);
			CHECK(strncmp(p.msg2, "Holed, ", 7) == 0 || strncmp(p.msg2, "Gimme, ", 7) == 0);
			{
				char b[24];
				snprintf(b, sizeof b, "%c-holed-%d-%d", resumed ? 'b' : 'a', r.hole + 1, p.cur + 1);
				snap(rt - 500, b);
			}
			f = play_tap(&p, 80, 60, t);
			tap_at(rt, 80, 60);
			rt += 300;
			if (f & PL_TURN) {
				turns++;
				CHECK(f & PL_RENDER);
				snap(rt + 1200, resumed ? "b-turn" : "a-turn");
				rt += IIIX_SCENE_MS;
				continue;
			}
			CHECK(f == PL_CARD && p.view == PLAY_CARD);
			if (r.over) {
				UiDialog d;
				CHECK(p.card == CARD_FINAL);
				card_layout(&d, &r, p.card, p.page);
				snap(rt + 1500, "b-card-final");
				tap_at(rt + 2500, d.btn[0].x + d.btn[0].w / 2, d.btn[0].y + d.btn[0].h / 2);
				snap(rt + 5000, "b-clubhouse");
				break;
			}
			CHECK(p.card == CARD_HOLE && r.hole == hole + 1);
			{
				UiDialog d;
				char b[24];
				card_layout(&d, &r, p.card, p.page);
				snprintf(b, sizeof b, "%c-card-hole%d", resumed ? 'b' : 'a', hole + 1);
				snap(rt + 1500, b);
				tap_at(rt + 2500, d.btn[0].x + d.btn[0].w / 2, d.btn[0].y + d.btn[0].h / 2);
				snap(rt + 2500 + 1500, resumed ? "b-next-hole" : "a-next-hole");
				rt += 2500 + IIIX_HOLE_MS + IIIX_SCENE_MS;
			}
			if (hole == 0) {
				gfx_fill(&s, 0, 0, 160, 160, UIC_BAR);
				play_card(&s, &p);
				save(&s, "card-hole1");
				/* the card doesn't let go with other keys */
				CHECK(play_key(&p, KEY_LEFT, t) == 0 && play_key(&p, KEY_OTHER, t) == 0);
			}
			/* the honour: the lower score on the hole just played, a tie keeping the order */
			{
				uint8_t a = r.pl[0].score[hole], b = r.pl[1].score[hole];
				CHECK(r.order[0] == (a < b ? 0 : b < a ? 1 : honour));
				honour = r.order[0];
			}
			CHECK(play_key(&p, KEY_SELECT, t) == PL_NEXT);
			hole++;
			play_start(&p, &holes[r.hole].h, &holes[r.hole].g, &r);
			CHECK(p.cur == r.order[0] && p.stroke == 1 && p.lie == LIE_TEE);
			CHECK(p.rv.ball.x == holes[r.hole].h.tees[0].pos.x && p.rv.ball.y == holes[r.hole].h.tees[0].pos.y);
			play_frame(&p, &s, &bg, t);
			continue;
		}
		if (f & PL_TURN) {
			turns++;
			if (!turn_strip) {
				play_turn(&s, &p);
				turn_strip = 1;
			}
		}
		CHECK(f & PL_RENDER);
		play_frame(&p, &s, &bg, t);

		/* the scorecard from the map mid-hole, and back */
		if (shots == 3) {
			UiDialog d;
			/* the same on the IIIx: Map on the panel, Card on the map's, OK */
			tap_at(rt, 119, 153);
			snap(rt + 1500, "a-map");
			tap_at(rt + 2000, 116, 156);
			snap(rt + 3500, "a-card");
			card_layout(&d, &r, CARD_VIEW, 0);
			tap_at(rt + 4000, d.btn[0].x + d.btn[0].w / 2, d.btn[0].y + d.btn[0].h / 2);
			rt += 5500;
			CHECK(play_key(&p, KEY_OTHER, t) == PL_MAP);
			play_map(&s, &p);
			play_status(&s, &p, NULL);
			save(&s, "map-card-button");
			CHECK(play_tap(&p, 116, 156, t) == PL_CARD && p.card == CARD_VIEW);
			/* opened from the map, it closes back to the map */
			CHECK(play_key(&p, KEY_SELECT, t) == PL_MAP && p.view == PLAY_MAP);
			CHECK(play_key(&p, KEY_SELECT, t) == PL_SCENE && p.view == PLAY_SCENE);
			CHECK(play_key(&p, KEY_CARD, t) == PL_CARD && p.view == PLAY_CARD);
			gfx_fill(&s, 0, 0, 160, 160, UIC_BAR);
			play_card(&s, &p);
			save(&s, "card-view");
			CHECK(play_key(&p, KEY_CARD, t) == PL_SCENE && p.view == PLAY_SCENE);
			play_frame(&p, &s, &bg, t);
		}

		/* in the air: settling counts the shot where it lands */
		if (!settled && r.hole == 2 && shots > 0) {
			Round rr = r;
			q = p;
			q.round = &rr;
			uint32_t u = t + 1000;
			CHECK(play_key(&q, KEY_SELECT, u) & PL_ANIM);
			/* the meter runs out: a full putt, or a mis-hit */
			while (q.state != PS_FLY && u < t + 10000) play_frame(&q, &s, &bg, u += 20);
			CHECK(q.state == PS_FLY);
			k = rr.pl[q.cur].taken;
			play_settle(&q);
			CHECK(rr.pl[q.cur].taken > k || rr.pl[q.cur].holed);
			CHECK(q.state == PS_AIM || q.state == PS_DONE);
			settled = 1;
		}
	}
	printf("round: %d shots, %d turn changes, scores", shots, turns);
	for (k = 0; k < r.players; k++) {
		int h;
		printf(" %s", r.pl[k].name);
		for (h = 0; h < r.holes; h++) printf(" %d", r.pl[k].score[h]);
		printf(" (%d, %+d)", rules_strokes(&r, (uint8_t)k, 0, r.holes), rules_to_par(&r, (uint8_t)k));
	}
	printf("\n");
	CHECK(r.over && resumed && settled && turns >= 6);
	for (k = 0; k < 2; k++) {
		int h;
		for (h = 0; h < 3; h++) CHECK(r.pl[k].score[h] > 0);
	}
	gfx_fill(&s, 0, 0, 160, 160, UIC_BAR);
	play_card(&s, &p);
	save(&s, "card-final");
	gfx_fill(&s2, 0, 0, 320, 320, UIC_BAR);
	play_card(&s2, &p);
	save(&s2, "card-final");
	CHECK(card_stats_button(&r, CARD_FINAL) == 1);
	CHECK(play_key(&p, KEY_CARD, t) == PL_CARD && p.card == CARD_STATS && p.stat_player == 0);
	gfx_fill(&s, 0, 0, 160, 160, UIC_BAR);
	play_card(&s, &p);
	save(&s, "round-stats");
	gfx_fill(&s2, 0, 0, 320, 320, UIC_BAR);
	play_card(&s2, &p);
	save(&s2, "round-stats");
	CHECK(play_key(&p, KEY_DOWN, t) == PL_CARD && p.stat_player == 1);
	CHECK(play_key(&p, KEY_UP, t) == PL_CARD && p.stat_player == 0);
	CHECK(play_key(&p, KEY_BACK, t) == PL_CARD && p.card == CARD_FINAL);
	CHECK(play_key(&p, KEY_RIGHT, t) == PL_CARD && p.csel == 0);
	if (rf) fclose(rf);
	rf = NULL;
	CHECK(play_key(&p, KEY_SELECT, t) == PL_BACK);
	/* settling a round that's over leaves it over */
	play_settle(&p);
	CHECK(r.over);
	free(s.bits);
	free(bg.bits);
	free(s2.bits);
}

/* The card's box stays on the screen for every size of round, and taps hit its buttons. */
static void card_tests(void)
{
	Round r;
	UiDialog d;
	Surface s, s2;
	uint8_t pars[36], n, pl, page, mode;

	memset(pars, 4, sizeof pars);
	surface(&s, 1);
	surface(&s2, 2);
	for (n = 1; n <= 36; n++)
		for (pl = 1; pl <= 4; pl++)
			for (mode = 0; mode < 3; mode++)
				for (page = 0; page < card_pages(&r) || page == 0; page++) {
					uint8_t i;
					rules_init(&r, "x", pl, n, pars, 1);
					card_layout(&d, &r, mode, page);
					CHECK(d.box.x >= 0 && d.box.y >= 0 && d.box.x + d.box.w <= 160 && d.box.y + d.box.h <= 160);
					CHECK(d.count == (n > 9 ? 2 : 1) + (mode == CARD_FINAL));
					CHECK(card_hit(&r, mode, page, d.box.x + 4, d.box.y + 20) == -1);
					for (i = 0; i < d.count; i++) {
						CHECK(d.btn[i].x >= d.box.x && d.btn[i].x + d.btn[i].w <= d.box.x + d.box.w);
						CHECK(card_hit(&r, mode, page, d.btn[i].x + 2, d.btn[i].y + 2) == i);
					}
				}

	/* 18 holes for 4: the front and back nines, from a play with a round to switch pages */
	{
		static Play p;
		round18(&r, 18);
		memset(&p, 0, sizeof p);
		p.round = &r;
		p.view = PLAY_CARD;
		p.card = CARD_FINAL;
		backdrop(&s);
		play_card(&s, &p);
		save(&s, "card18-front");
		backdrop(&s2);
		play_card(&s2, &p);
		save(&s2, "card18-front");
		CHECK(play_key(&p, KEY_RIGHT, 0) == PL_CARD && p.csel == 1);
		CHECK(play_key(&p, KEY_SELECT, 0) == PL_CARD && p.page == 1);
		backdrop(&s);
		play_card(&s, &p);
		save(&s, "card18-back");
		backdrop(&s2);
		play_card(&s2, &p);
		save(&s2, "card18-back");
		CHECK(play_key(&p, KEY_DOWN, 0) == PL_CARD && p.page == 0);
		CHECK(play_key(&p, KEY_LEFT, 0) == PL_CARD && p.csel == 0);
		CHECK(play_key(&p, KEY_SELECT, 0) == PL_BACK);
		/* part way, one player */
		round18(&r, 11);
		r.players = 1;
		p.card = CARD_VIEW;
		p.page = 1;
		p.csel = 0;
		backdrop(&s);
		play_card(&s, &p);
		save(&s, "card18-1p");
		CHECK(play_key(&p, KEY_SELECT, 0) == PL_SCENE && p.view == PLAY_SCENE);
	}
	free(s.bits);
	free(s2.bits);
}

static void dana_card_tests(void)
{
	Surface s = {0};
	Round r;
	static Play p;
	uint8_t before[8][368];
	int x, y, front = 0, back = 0;

	s.w = s.rowBytes = 368;
	s.h = 160;
	s.depth = 8;
	s.scale = 1;
	s.bits = calloc(s.rowBytes, s.h);
	gfx_init(&s);
	s.fullview = 1;
	rules_init(&r, "Dana test", 1, 18, pars18, 1);
	p = (Play){0};
	p.round = &r;
	p.view = PLAY_CARD;
	p.card = CARD_VIEW;
	p.card_from = PLAY_MAP;

	play_card(&s, &p);
	memcpy(before, s.bits + 28 * s.rowBytes, sizeof before);
	r.pl[0].score[0] = 4;
	r.pl[0].score[9] = 5;
	play_card(&s, &p);
	/* The first score on each nine shares a row, on opposite sides. */
	for (y = 0; y < 8; y++)
		for (x = 0; x < s.lw; x++)
			if (before[y][x] != s.bits[(28 + y) * s.rowBytes + x]) {
				if (x < s.lw / 2) front++;
				else back++;
			}
	CHECK(front > 0 && back > 0);
	CHECK(play_key(&p, KEY_RIGHT, 0) == 0 && p.csel == 0);
	CHECK(play_key(&p, KEY_DOWN, 0) == 0 && p.page == 0);
	CHECK(play_key(&p, KEY_BACK, 0) == PL_BACK);
	CHECK(play_key(&p, KEY_SELECT, 0) == PL_MAP && p.view == PLAY_MAP);

	p.view = PLAY_CARD;
	CHECK(play_tap(&p, 184, 148, 0) == PL_MAP && p.view == PLAY_MAP);
	p.view = PLAY_CARD;
	p.card = CARD_FINAL;
	CHECK(play_key(&p, KEY_RIGHT, 0) == PL_CARD && p.csel == 2);
	CHECK(play_key(&p, KEY_SELECT, 0) == PL_CARD && p.card == CARD_STATS);
	CHECK(play_key(&p, KEY_BACK, 0) == PL_CARD && p.card == CARD_FINAL && p.csel == 2);
	free(s.bits);
}

/* ---- game modes, computer golfers and records (milestone 12) ---- */

/* Every player's score on the hole being played, then on to the next. */
static void hole_scores(Round *r, const uint8_t *sc)
{
	uint8_t i;
	for (i = 0; i < r->players; i++) rules_hole_out(r, i, sc[i]);
	rules_advance(r);
}

static const char *card_title_of(const Round *r, uint8_t mode)
{
	static UiDialog d;
	card_layout(&d, r, mode, 0);
	return d.title;
}

static void match_tests(void)
{
	Round r;
	uint8_t pars[18], k, left;
	static const uint8_t win1[2] = { 3, 4 }, win2[2] = { 5, 4 }, half[2] = { 4, 4 };

	memset(pars, 4, sizeof pars);
	/* 3 up after 3, halved to the 16th: "3 and 2" */
	rules_init(&r, "x", 2, 18, pars, 1);
	rules_mode(&r, MODE_MATCH, NULL, 0);
	for (k = 0; k < 3; k++) hole_scores(&r, win1);
	CHECK(rules_match(&r, &left) == 3 && left == 15 && !r.over);
	CHECK(rules_hole_win(&r, 0) == 0 && rules_hole_win(&r, 3) == 3);
	CHECK(strcmp(card_title_of(&r, CARD_VIEW), "P1 3 up") == 0);
	for (k = 3; k < 15; k++) hole_scores(&r, half);
	CHECK(rules_hole_win(&r, 4) == 2);
	CHECK(rules_match(&r, &left) == 3 && left == 3 && !r.over && r.hole == 15);
	hole_scores(&r, half);
	CHECK(r.over && rules_match(&r, &left) == 3 && left == 2 && rules_done(&r) == 16);
	CHECK(strcmp(card_title_of(&r, CARD_FINAL), "P1 wins 3 & 2") == 0);
	CHECK(r.pl[0].score[16] == 0);

	/* all square to the end: halved; won on the last: "1 up" */
	rules_init(&r, "x", 2, 18, pars, 1);
	rules_mode(&r, MODE_MATCH, NULL, 0);
	for (k = 0; k < 17; k++) hole_scores(&r, k & 1 ? win1 : win2);
	CHECK(rules_match(&r, &left) == -1 && left == 1 && !r.over);
	CHECK(strcmp(card_title_of(&r, CARD_VIEW), "P2 1 up") == 0);
	hole_scores(&r, win1);
	CHECK(r.over && rules_match(&r, &left) == 0);
	CHECK(strcmp(card_title_of(&r, CARD_FINAL), "Match halved") == 0);
	rules_init(&r, "x", 2, 18, pars, 1);
	rules_mode(&r, MODE_MATCH, NULL, 0);
	for (k = 0; k < 17; k++) hole_scores(&r, half);
	CHECK(strcmp(card_title_of(&r, CARD_VIEW), "All square") == 0);
	hole_scores(&r, win2);
	CHECK(r.over && strcmp(card_title_of(&r, CARD_FINAL), "P2 wins 1 up") == 0);
	/* dormie isn't over: 2 up with 2 to play */
	rules_init(&r, "x", 2, 4, pars, 1);
	rules_mode(&r, MODE_MATCH, NULL, 0);
	hole_scores(&r, win1);
	hole_scores(&r, win1);
	CHECK(!r.over && rules_match(&r, &left) == 2 && left == 2);
	hole_scores(&r, win1);
	CHECK(r.over && strcmp(card_title_of(&r, CARD_FINAL), "P1 wins 3 & 1") == 0);

	/* best ball: each side's better score */
	{
		static const uint8_t a[4] = { 5, 3, 4, 4 }, b[4] = { 4, 4, 4, 5 }, c[4] = { 6, 6, 4, 6 };
		rules_init(&r, "x", 4, 3, pars, 1);
		rules_mode(&r, MODE_BEST, NULL, 0);
		CHECK(rules_side(&r, 0) == 0 && rules_side(&r, 1) == 0 && rules_side(&r, 2) == 1 && rules_side(&r, 3) == 1);
		hole_scores(&r, a);
		CHECK(rules_hole_win(&r, 0) == 0);
		hole_scores(&r, b);
		CHECK(rules_hole_win(&r, 1) == 2 && rules_match(&r, &left) == 1 && left == 1 && !r.over);
		CHECK(strcmp(card_title_of(&r, CARD_VIEW), "P1+P2 1 up") == 0);
		hole_scores(&r, c);
		CHECK(r.over && rules_match(&r, &left) == 0 && strcmp(card_title_of(&r, CARD_FINAL), "Match halved") == 0);
		rules_init(&r, "x", 4, 3, pars, 1);
		rules_mode(&r, MODE_BEST, NULL, 0);
		hole_scores(&r, c);
		hole_scores(&r, c);
		CHECK(r.over && strcmp(card_title_of(&r, CARD_FINAL), "P3+P4 win 2 & 1") == 0);
	}
	CHECK(rules_mode_ok(MODE_MATCH, 2) && !rules_mode_ok(MODE_MATCH, 3) && rules_mode_ok(MODE_BEST, 4) &&
	      !rules_mode_ok(MODE_BEST, 2) && !rules_mode_ok(MODE_SKINS, 1) && rules_mode_ok(MODE_SKINS, 3) &&
	      rules_mode_ok(MODE_TOURN, 1) && rules_mode_ok(MODE_RANGE, 1) && !rules_mode_ok(MODE_GREEN, 2));
}

static void skins_tests(void)
{
	Round r;
	uint8_t pars[6], won[RULES_PLAYERS], carry;
	static const uint8_t h1[3] = { 4, 4, 5 }, h2[3] = { 3, 4, 4 }, h3[3] = { 4, 4, 4 }, h4[3] = { 5, 4, 3 }, h5[3] = { 4, 3, 3 };

	memset(pars, 4, sizeof pars);
	rules_init(&r, "x", 3, 6, pars, 1);
	rules_mode(&r, MODE_SKINS, NULL, 0);
	hole_scores(&r, h1);
	rules_skins(&r, won, &carry);
	CHECK(carry == 1 && won[0] == 0 && won[1] == 0);
	CHECK(strcmp(card_title_of(&r, CARD_VIEW), "Skins, 1 carried") == 0);
	hole_scores(&r, h2);
	rules_skins(&r, won, &carry);
	CHECK(carry == 0 && won[0] == 2);
	CHECK(strcmp(card_title_of(&r, CARD_VIEW), "Scorecard") == 0);
	hole_scores(&r, h3);
	hole_scores(&r, h4);
	rules_skins(&r, won, &carry);
	CHECK(carry == 0 && won[0] == 2 && won[1] == 0 && won[2] == 2);
	/* a tie for the lowest carries, even with a higher score alone */
	hole_scores(&r, h5);
	rules_skins(&r, won, &carry);
	CHECK(carry == 1 && won[2] == 2);
	hole_scores(&r, h5);
	rules_skins(&r, won, &carry);
	CHECK(r.over && carry == 2 && won[0] == 2 && won[1] == 0 && won[2] == 2);
	CHECK(strcmp(card_title_of(&r, CARD_FINAL), "Final card") == 0);
	{
		Surface s;
		static Play p;
		surface(&s, 1);
		memset(&p, 0, sizeof p);
		p.round = &r;
		p.view = PLAY_CARD;
		p.card = CARD_FINAL;
		backdrop(&s);
		play_card(&s, &p);
		save(&s, "m12-card-skins");
		free(s.bits);
	}
}

static void tournament_tests(void)
{
	Round r, q;
	Course c = {0};
	BoardLine b[RULES_PLAYERS + RULES_FIELD];
	uint8_t buf[RULES_SAVE_MAX];
	uint8_t pars[18] = { 4, 4, 3, 5, 4, 4, 3, 4, 5, 4, 3, 4, 5, 4, 4, 3, 5, 4 }, n, i, h, g;
	long sum[RULES_FIELD] = { 0 }, all = 0;
	int diff = 0;
	uint16_t len;
	uint32_t seed;
	static const uint8_t sc[2] = { 4, 5 };

	/* the same seed, the same field; another seed, another */
	rules_init(&r, "Par386 Auld Kirk", 2, 18, pars, 77);
	rules_init(&q, "x", 2, 18, pars, 78);
	for (g = 0; g < RULES_FIELD; g++)
		for (h = 0; h < 18; h++) {
			uint8_t v = rules_field_score(&r, g, h);
			CHECK(v == rules_field_score(&r, g, h));
			CHECK(v >= pars[h] - 2 && v <= pars[h] + 3 && (pars[h] > 3 || v >= pars[h] - 1));
			diff += v != rules_field_score(&q, g, h);
		}
	CHECK(diff > 40);
	/* the stronger the golfer, the lower: the first two average under par, the last two over */
	for (seed = 1; seed <= 300; seed++) {
		rules_init(&q, "x", 1, 18, pars, seed);
		for (g = 0; g < RULES_FIELD; g++)
			for (h = 0; h < 18; h++) {
				int d = rules_field_score(&q, g, h) - pars[h];
				sum[g] += d;
				all += d;
			}
	}
	CHECK(sum[0] + sum[1] < 0 && sum[6] + sum[7] > 0 && sum[0] + sum[1] < sum[6] + sum[7]);
	printf("field: mean to par a round by strength %.2f %.2f %.2f %.2f\n", (sum[0] + sum[1]) / 600.0,
	       (sum[2] + sum[3]) / 600.0, (sum[4] + sum[5]) / 600.0, (sum[6] + sum[7]) / 600.0);
	CHECK(all > -300L * 18 && all < 300L * 18 * 2);

	/* the board: everyone, best first, ties sharing a place */
	rules_mode(&r, MODE_TOURN, NULL, 0);
	c.wind = 15;
	c.green = 13;
	for (h = 0; h < r.holes; h++) c.yards[h] = pars[h] == 3 ? 150 : pars[h] == 4 ? 380 : 520;
	c.yards[1] += 80;
	rules_tour_course(&r, &c);
	CHECK(r.tour_stage == TOUR_FIRST && r.tour_wind == 15 && r.tour_difficulty[1] > r.tour_difficulty[0]);
	q = r;
	{
		int wind = 0, long_hole = 0;
		for (seed = 1; seed <= 60; seed++) {
			q.seed = seed;
			q.tour_wind = 2;
			for (g = 0; g < RULES_FIELD; g++) {
				uint8_t calm = rules_field_score(&q, g, 0);
				q.tour_wind = 25;
				wind += (int)rules_field_score(&q, g, 0) - calm;
				q.tour_wind = 2;
				calm = rules_field_score(&q, g, 1);
				q.tour_difficulty[1] = r.tour_difficulty[0];
				long_hole += (int)calm - rules_field_score(&q, g, 1);
				q.tour_difficulty[1] = r.tour_difficulty[1];
			}
		}
		CHECK(wind > 0 && long_hole > 0);
	}
	len = rules_pack(&r, buf);
	CHECK(rules_unpack(&q, buf, len) == 0 && memcmp(&r, &q, sizeof r) == 0);
	CHECK(rules_unpack(&q, buf, len - 1) == -1);
	buf[1] = 4;
	CHECK(rules_unpack(&q, buf, len - RULES_STATS_SAVE(r.players) - 3 - r.holes - 2 * r.players) == 0 &&
	      q.tour_stage == TOUR_SINGLE && !q.stats_available);
	n = rules_board(&r, b);
	CHECK(n == 2 + RULES_FIELD);
	for (i = 0; i < n; i++) CHECK(b[i].to_par == 0 && b[i].pos == 1 && b[i].tied);
	CHECK(b[0].who == 0 && b[1].who == 1 && b[2].who == RULES_PLAYERS);
	for (h = 0; h < 9; h++) hole_scores(&r, sc);
	n = rules_board(&r, b);
	for (i = 1; i < n; i++) {
		CHECK(b[i].to_par >= b[i - 1].to_par);
		CHECK(b[i].pos == (b[i].to_par == b[i - 1].to_par ? b[i - 1].pos : i + 1));
	}
	for (i = 0; i < n; i++) {
		int16_t want = 0;
		for (h = 0; h < 9; h++)
			want += (b[i].who < RULES_PLAYERS ? r.pl[b[i].who].score[h] : rules_field_score(&r, b[i].who - RULES_PLAYERS, h)) - pars[h];
		CHECK(b[i].to_par == want);
		if (b[i].who == 1) CHECK(want == 9);
	}
	CHECK(strcmp(rules_field_name(0), "MacLeod") == 0 && strlen(rules_field_name(7)) <= 8);
	{
		Surface s, s2;
		static Play p;
		UiDialog d;
		surface(&s, 1);
		surface(&s2, 2);
		memset(&p, 0, sizeof p);
		p.round = &r;
		p.view = PLAY_CARD;
		p.card = CARD_HOLE;
		p.board = 1;
		p.geo.lw = p.geo.lh = 160;
		backdrop(&s);
		play_card(&s, &p);
		save(&s, "m12-board");
		backdrop(&s2);
		play_card(&s2, &p);
		save(&s2, "m12-board");
		board_layout(&d, &r, CARD_HOLE, n);
		CHECK(d.box.x >= 0 && d.box.y >= 0 && d.box.x + d.box.w <= 160 && d.box.y + d.box.h <= 160);
		CHECK(strcmp(d.title, "Kirk Open") == 0 && strcmp(d.text, "Round 1 after 9") == 0 && d.count == 2);
		/* the board's Card button shows the card, whose Next hole goes on */
		CHECK(play_key(&p, KEY_RIGHT, 0) == PL_CARD && p.csel == 1);
		CHECK(play_key(&p, KEY_SELECT, 0) == PL_CARD && !p.board);
		CHECK(play_tap(&p, 1, 1, 0) == 0);
		CHECK(play_key(&p, KEY_SELECT, 0) == PL_NEXT);
		/* from the map's card in a tournament: OK, the other nine and the board */
		p.card = CARD_VIEW;
		p.board = 0;
		p.csel = 0;
		CHECK(card_buttons(&r, CARD_VIEW) == 3 && card_board_button(&r, CARD_VIEW) == 2);
		CHECK(card_board_button(&r, CARD_HOLE) == 0xFF);
		card_layout(&d, &r, CARD_VIEW, 0);
		CHECK(d.box.x + d.box.w <= 160 && d.btn[2].x + d.btn[2].w <= d.box.x + d.box.w);
		CHECK(play_tap(&p, d.btn[2].x + 2, d.btn[2].y + 2, 0) == PL_CARD && p.board);
		backdrop(&s);
		play_card(&s, &p);
		save(&s, "m12-board-view");
		CHECK(play_key(&p, KEY_SELECT, 0) == PL_SCENE && p.view == PLAY_SCENE);
		/* The first round ends at its leaderboard. Save there and resume it. */
		for (h = 9; h < 18; h++) hole_scores(&r, sc);
		CHECK(!r.over && r.tour_stage == TOUR_BREAK && rules_done(&r) == 18);
		len = rules_pack(&r, buf);
		CHECK(rules_unpack(&q, buf, len) == 0 && memcmp(&r, &q, sizeof r) == 0);
		board_layout(&d, &r, CARD_FINAL, n);
		CHECK(strcmp(d.text, "Round 1 leaders") == 0 && strcmp(d.buttons, "Round 2|Card") == 0);
		play_start(&p, &holes[0].h, &holes[0].g, &q);
		CHECK(p.view == PLAY_CARD && p.card == CARD_FINAL && p.board);
		backdrop(&s);
		play_card(&s, &p);
		save(&s, "tournament-round1");
		backdrop(&s2);
		play_card(&s2, &p);
		save(&s2, "tournament-round1");
		CHECK(play_key(&p, KEY_SELECT, 0) == PL_NEXT && q.tour_stage == TOUR_SECOND && q.hole == 0);
		CHECK(q.tour_first[0] == 72 && q.tour_first[1] == 90 && !rules_done(&q));
		CHECK(rules_round_seed(&q) != rules_round_seed(&r) && rules_tour_to_par(&q, 1) == 18);
		{
			BoardLine first[RULES_PLAYERS + RULES_FIELD];
			uint8_t count = rules_board(&r, first);
			CHECK(rules_board(&q, b) == count);
			for (i = 0; i < count; i++) CHECK(b[i].who == first[i].who && b[i].to_par == first[i].to_par);
		}
		for (h = 0; h < 9; h++) hole_scores(&q, sc);
		len = rules_pack(&q, buf);
		CHECK(rules_unpack(&r, buf, len) == 0 && memcmp(&r, &q, sizeof r) == 0);
		CHECK(rules_tour_to_par(&q, 1) == 27);
		n = rules_board(&q, b);
		board_layout(&d, &q, CARD_HOLE, n);
		CHECK(strcmp(d.text, "Round 2 after 9") == 0);
		p.card = CARD_HOLE;
		p.csel = 0;
		backdrop(&s);
		play_card(&s, &p);
		save(&s, "tournament-round2");
		backdrop(&s2);
		play_card(&s2, &p);
		save(&s2, "tournament-round2");
		for (h = 9; h < 18; h++) hole_scores(&q, sc);
		board_layout(&d, &q, CARD_FINAL, n);
		CHECK(q.over && strcmp(d.text, "Final leaders") == 0);
		CHECK(rules_tour_to_par(&q, 1) == 36);
		free(s.bits);
		free(s2.bits);
	}
}

/* Plays the round from where it is to the end through play.c, every player
 * a computer golfer, as the platform would: quick 1 for quick shots, 0 for
 * the full flight, 2 for the full flight going over to the map; frames every 20 ms, the card's
 * first button at the end of each hole. With every, the round is left that
 * often (settled, packed, unpacked into a new round) and started again, as
 * leaving the app and coming back. Returns the frames run. */
static long auto_round(Round *r, uint8_t quick, int every)
{
	static Play p;
	Surface s, bg, m;
	uint32_t t = 0;
	long n = 0, maps = 0;
	int f, resumes = 0;

	surface(&s, 1);
	surface(&bg, 1);
	surface(&m, 1);
	memset(&p, 0, sizeof p);
	p.quick = quick == 1;
	p.mapview = quick == 2;
	play_start(&p, &holes[r->hole].h, &holes[r->hole].g, r);
	while (!r->over && n < 1000000L) {
		n++;
		t += 20;
		/* the flight overlays draw over whatever is there */
		f = p.view == PLAY_CARD ? play_key(&p, KEY_SELECT, t) : play_frame(&p, &s, &bg, t);
		if (f & PL_BACK) break;
		/* the shot's map, 20 rows a frame */
		if (play_map_wanted(&p)) play_map_prep(&m, &p, 20);
		if (f & PL_MAP) {
			if (p.onmap) maps++;
			play_map(&s, &p);
			play_status(&s, &p, NULL);
		}
		if (f & PL_NEXT) {
			play_start(&p, &holes[r->hole].h, &holes[r->hole].g, r);
			continue;
		}
		if (every && n % every == 0 && p.view != PLAY_CARD) {
			uint8_t buf[RULES_SAVE_MAX];
			uint16_t len;
			Round q;
			play_settle(&p);
			if (r->over) break;
			len = rules_pack(r, buf);
			memset(&q, 0xA5, sizeof q);
			CHECK(rules_unpack(&q, buf, len) == 0);
			CHECK(q.mode == r->mode && q.pl[0].cpu == r->pl[0].cpu);
			*r = q;
			play_start(&p, &holes[r->hole].h, &holes[r->hole].g, r);
			resumes++;
		}
	}
	CHECK(r->over);
	CHECK(!every || resumes > 3);
	/* full shots went over to the map */
	CHECK(quick != 2 || maps > (every ? 0 : 4));
	free(s.bits);
	free(bg.bits);
	free(m.bits);
	return n;
}

static void same_scores(const Round *a, const Round *b)
{
	uint8_t i;
	for (i = 0; i < a->players; i++) CHECK(memcmp(a->pl[i].score, b->pl[i].score, a->holes) == 0);
	CHECK(a->over == b->over && a->hole == b->hole);
	CHECK(a->tour_stage == b->tour_stage && memcmp(a->tour_first, b->tour_first, sizeof a->tour_first) == 0);
}

/* A computer golfer's first shot at the start of a round, saved (settled) while
 * the result shows on the map: the round carries on to the next player, with
 * the shot counted once. */
static void cpu_start_save_test(const Course *c)
{
	static Play p, q;
	static const uint8_t lv[2] = { CPU_FAIR, CPU_HUMAN };
	Round r, o;
	Surface s, bg;
	uint32_t t = 0;
	int k, f;

	surface(&s, 1);
	surface(&bg, 1);
	rules_init(&r, "Par386 Test", 2, c->hole_count, c->par, 5);
	rules_mode(&r, MODE_STROKE, lv, 0);
	o = r;
	memset(&q, 0, sizeof q);
	q.quick = 1;
	play_start(&q, &holes[0].h, &holes[0].g, &o);
	memset(&p, 0, sizeof p);
	p.quick = 1;
	play_start(&p, &holes[0].h, &holes[0].g, &r);
	CHECK(p.state == PS_RESULT && p.view == PLAY_MAP && r.pl[0].taken == 0);
	play_settle(&p);
	play_settle(&p);
	CHECK(r.pl[0].taken >= 1 && p.state == PS_RESULT && p.view == PLAY_MAP);
	for (k = 0; k < 200 && p.cur == 0; k++) {
		t += 20;
		f = play_frame(&p, &s, &bg, t);
		(void)f;
	}
	CHECK(p.cur == 1 && p.view == PLAY_SCENE && r.turn == 1);
	/* the same as a round that wasn't saved */
	for (k = 0; k < 200 && q.cur == 0; k++) {
		t += 20;
		play_frame(&q, &s, &bg, t);
	}
	CHECK(q.cur == 1 && r.pl[0].taken == o.pl[0].taken && r.pl[0].ball.x == o.pl[0].ball.x && r.pl[0].ball.y == o.pl[0].ball.y);
	free(s.bits);
	free(bg.bits);
}

static void cpu_tests(const Course *c)
{
	Round a, b;
	static const uint8_t four[4] = { CPU_EASY, CPU_FAIR, CPU_GOOD, CPU_PRO };
	static const uint8_t pros[4] = { CPU_PRO, CPU_PRO, CPU_PRO, CPU_PRO }, easy[4] = { CPU_EASY, CPU_EASY, CPU_EASY, CPU_EASY };
	long frames;
	int lvl, tot[5] = { 0 }, seed, k, n = 0;
	uint8_t m;

	/* the same round twice; left and picked up again on the way; the full flight instead of the quick result */
	rules_init(&a, "Par386 Test", 4, c->hole_count, c->par, 11);
	rules_mode(&a, MODE_STROKE, four, 0);
	CHECK(strcmp(a.pl[1].name, "Angus") == 0 && a.pl[3].cpu == CPU_PRO);
	b = a;
	frames = auto_round(&a, 1, 0);
	auto_round(&b, 1, 0);
	same_scores(&a, &b);
	printf("cpu: 4 computer golfers, 3 holes quick: %ld frames (%.0f s), scores", frames, frames * 0.02);
	for (k = 0; k < 4; k++) printf(" %d", rules_strokes(&a, k, 0, a.holes));
	printf("\n");
	rules_init(&b, "Par386 Test", 4, c->hole_count, c->par, 11);
	rules_mode(&b, MODE_STROKE, four, 0);
	auto_round(&b, 1, 37);
	same_scores(&a, &b);
	rules_init(&b, "Par386 Test", 4, c->hole_count, c->par, 11);
	rules_mode(&b, MODE_STROKE, four, 0);
	frames = auto_round(&b, 0, 0);
	same_scores(&a, &b);
	printf("cpu: the same with the flight: %ld frames (%.0f s)\n", frames, frames * 0.02);
	rules_init(&b, "Par386 Test", 4, c->hole_count, c->par, 11);
	rules_mode(&b, MODE_STROKE, four, 0);
	auto_round(&b, 0, 53);
	same_scores(&a, &b);
	/* and with the full shots going over to the map, played through and left on the way */
	rules_init(&b, "Par386 Test", 4, c->hole_count, c->par, 11);
	rules_mode(&b, MODE_STROKE, four, 0);
	frames = auto_round(&b, 2, 0);
	same_scores(&a, &b);
	printf("cpu: the same with the shot view on the map: %ld frames (%.0f s)\n", frames, frames * 0.02);
	rules_init(&b, "Par386 Test", 4, c->hole_count, c->par, 11);
	rules_mode(&b, MODE_STROKE, four, 0);
	auto_round(&b, 2, 113);
	same_scores(&a, &b);

	/* save and resume in every mode gives the same round as playing it through */
	for (m = MODE_MATCH; m <= MODE_TOURN; m++) {
		uint8_t pl = m == MODE_MATCH ? 2 : m == MODE_BEST ? 4 : 3;
		rules_init(&a, "Par386 Test", pl, c->hole_count, c->par, 5 + m);
		rules_mode(&a, m, four, 0);
		b = a;
		auto_round(&a, 1, 0);
		auto_round(&b, 1, 29 + m);
		same_scores(&a, &b);
		CHECK(b.mode == m);
	}

	/* skill shows: pros score lower than the easy ones over a few rounds */
	for (seed = 1; seed <= 6; seed++)
		for (lvl = 0; lvl < 2; lvl++) {
			rules_init(&a, "Par386 Test", 4, c->hole_count, c->par, (uint32_t)seed * 101);
			rules_mode(&a, MODE_STROKE, lvl ? pros : easy, 0);
			auto_round(&a, 1, 0);
			for (k = 0; k < 4; k++) tot[lvl ? CPU_PRO : CPU_EASY] += rules_strokes(&a, k, 0, a.holes);
			n++;
		}
	printf("cpu: test course (par %d), mean of 24 rounds: easy %.2f, pro %.2f\n", rules_par(&a, 0, a.holes),
	       tot[CPU_EASY] / 24.0, tot[CPU_PRO] / 24.0);
	CHECK(tot[CPU_PRO] < tot[CPU_EASY]);
	CHECK(tot[CPU_PRO] <= 24 * (rules_par(&a, 0, a.holes) + 3));
	(void)n;
}

/* The panel's row B in each mode while aiming, and a computer golfer's result on the map. */
static void panel_tests(const Course *c)
{
	static Play p;
	Round r;
	Surface s, bg, s2;
	uint8_t m;
	static const uint8_t cpu2[2] = { CPU_HUMAN, CPU_FAIR };
	static const char *names[MODE_COUNT] = { "stroke", "match", "skins", "best", "tourn", "range", "green" };

	surface(&s, 1);
	surface(&bg, 1);
	surface(&s2, 2);
	for (m = 0; m < MODE_COUNT; m++) {
		uint8_t pl = m == MODE_MATCH || m == MODE_TOURN ? 2 : m == MODE_BEST ? 4 : m == MODE_SKINS ? 3 : 1;
		char name[32];
		rules_init(&r, "Par386 Test", pl, c->hole_count, c->par, 3);
		rules_mode(&r, m, NULL, m == MODE_RANGE ? 2 : 0);
		if (!MODE_PRACTICE(m)) {
			/* the first hole played: player 1 won it by 2, the others tied */
			uint8_t sc[4] = { 3, 5, 5, 5 };
			if (m == MODE_SKINS) sc[0] = 5;
			hole_scores(&r, sc);
		} else {
			r.balls = 4;
		}
		memset(&p, 0, sizeof p);
		play_start(&p, &holes[r.hole].h, &holes[r.hole].g, &r);
		gfx_fill(&s, 0, 0, 160, 160, UIC_BAR);
		play_frame(&p, &s, &bg, 0);
		snprintf(name, sizeof name, "m12-rowb-%s", names[m]);
		save(&s, name);
		gfx_fill(&s2, 0, 0, 320, 320, UIC_BAR);
		play_frame(&p, &s2, &bg, 0);
		p.redraw = 8;
		p.cam_ok = 0;
		play_frame(&p, &s2, NULL, 0);
		save(&s2, name);
		if (m == MODE_RANGE) CHECK(p.rv.hole->par == 5 && p.lie == LIE_TEE);
		if (m == MODE_GREEN) CHECK(p.lie == LIE_GREEN && p.club == CLUB_PT);
	}

	/* a computer golfer's tee shot on the map, then the score after they hole out */
	rules_init(&r, "Par386 Test", 2, c->hole_count, c->par, 3);
	rules_mode(&r, MODE_MATCH, cpu2, 0);
	r.order[0] = 1;
	r.order[1] = 0;
	r.turn = 1;
	memset(&p, 0, sizeof p);
	p.quick = 1;
	play_start(&p, &holes[0].h, &holes[0].g, &r);
	CHECK(p.cur == 1 && p.cpu == CPU_FAIR && p.view == PLAY_MAP && p.state == PS_RESULT && p.have_shot);
	/* keys don't swing for them; one hurries them on */
	CHECK(play_key(&p, KEY_SELECT, 0) == PL_ANIM);
	CHECK(play_frame(&p, &s, NULL, 0) & (PL_RENDER | PL_TURN));
	CHECK(p.cur == 0 && !p.cpu && p.view == PLAY_SCENE);
	rules_init(&r, "Par386 Test", 2, c->hole_count, c->par, 3);
	rules_mode(&r, MODE_MATCH, cpu2, 0);
	r.order[0] = 1;
	r.order[1] = 0;
	r.turn = 1;
	memset(&p, 0, sizeof p);
	p.quick = 1;
	play_start(&p, &holes[0].h, &holes[0].g, &r);
	CHECK(p.cpu == CPU_FAIR);
	gfx_fill(&s, 0, 0, 160, 160, UIC_BAR);
	play_map(&s, &p);
	play_status(&s, &p, NULL);
	save(&s, "m12-cpu-map");
	gfx_fill(&s2, 0, 0, 320, 320, UIC_BAR);
	play_map(&s2, &p);
	play_status(&s2, &p, NULL);
	save(&s2, "m12-cpu-map");
	CHECK(play_frame(&p, &s, NULL, 0) == PL_ANIM);
	CHECK(play_frame(&p, &s, NULL, PLAY_CPU_MS - 20) == PL_ANIM);
	CHECK(play_frame(&p, &s, NULL, PLAY_CPU_MS) & (PL_RENDER | PL_TURN));
	free(s.bits);
	free(bg.bits);
	free(s2.bits);
}

static void practice_tests(const Course *c)
{
	static Play p;
	Round r, q;
	Surface s, bg;
	uint32_t t = 0;
	uint8_t buf[RULES_SAVE_MAX], club;
	uint16_t len;
	vec2 tee, at;
	int f, k;

	surface(&s, 1);
	surface(&bg, 1);
	/* the range: every ball from the tee, the club and aim kept, the scene kept */
	rules_init(&r, "Par386 Test", 1, c->hole_count, c->par, 9);
	rules_mode(&r, MODE_RANGE, NULL, 2);
	CHECK(r.hole == 2 && r.mode == MODE_RANGE);
	play_start(&p, &holes[2].h, &holes[2].g, &r);
	tee = p.rv.ball;
	play_key(&p, KEY_DOWN, t);
	play_key(&p, KEY_LEFT, t);
	club = p.club;
	for (k = 0; k < 3; k++) {
		angle aim = p.aim;
		f = shot(&p, &s, &bg, &t);
		CHECK(f == PL_SCENE);
		CHECK(r.balls == (uint16_t)(k + 1) && p.rv.ball.x == tee.x && p.rv.ball.y == tee.y && p.lie == LIE_TEE);
		CHECK(p.club == club && p.aim == aim && p.state == PS_AIM && p.stroke == 1 && !r.over);
	}
	/* no card in practice */
	CHECK(play_key(&p, KEY_CARD, t) == 0 && p.view == PLAY_SCENE);
	len = rules_pack(&r, buf);
	CHECK(rules_unpack(&q, buf, len) == 0 && q.balls == 3 && q.mode == MODE_RANGE && q.hole == 2);

	/* the green: a ball 3 to 14 yd out on the green, putted out, then the next */
	rules_init(&r, "Par386 Test", 1, c->hole_count, c->par, 9);
	rules_mode(&r, MODE_GREEN, NULL, 0);
	memset(&p, 0, sizeof p);
	play_start(&p, &holes[0].h, &holes[0].g, &r);
	at = p.rv.ball;
	{
		fixed d = vec2_len(vec2_sub(at, holes[0].h.pins[0]));
		CHECK(p.lie == LIE_GREEN && p.club == CLUB_PT && d >= YD(3) - 2 && d <= YD(14) + 2);
		CHECK(surface_at(&holes[0].g, at.x, at.y) == SURF_GREEN);
	}
	/* the same ball after saving */
	len = rules_pack(&r, buf);
	CHECK(rules_unpack(&q, buf, len) == 0);
	memset(&p, 0, sizeof p);
	play_start(&p, &holes[0].h, &holes[0].g, &q);
	CHECK(p.rv.ball.x == at.x && p.rv.ball.y == at.y);
	/* Tap Repeat, then putt and resume from the same starting spot. */
	render_green(&bg, &p.rv);
	gfx_copy(&s, &bg, 0, 0, s.w, s.h);
	play_frame(&p, &s, &bg, t);
	save(&s, "short-game-practice");
	CHECK(play_tap(&p, 93, 153, t) == 0 && q.repeat);
	play_frame(&p, &s, &bg, t);
	save(&s, "short-game-repeat");
	CHECK(shot(&p, &s, &bg, &t) == PL_RENDER && !q.balls && p.stroke == 1);
	CHECK(p.rv.ball.x == at.x && p.rv.ball.y == at.y);
	len = rules_pack(&q, buf);
	CHECK(rules_unpack(&q, buf, len) == 0 && q.repeat);
	memset(&p, 0, sizeof p);
	play_start(&p, &holes[0].h, &holes[0].g, &q);
	CHECK(p.rv.ball.x == at.x && p.rv.ball.y == at.y);
	CHECK(play_key(&p, KEY_CARD, t) == 0 && !q.repeat);
	for (k = 0; k < 10 && q.balls == 0; k++) {
		f = shot(&p, &s, &bg, &t);
		CHECK(f == PL_RENDER);
	}
	CHECK(q.balls == 1 && p.stroke == 1 && p.lie == LIE_GREEN && (p.rv.ball.x != at.x || p.rv.ball.y != at.y));
	printf("practice green: first ball putted out in %d\n", k);
	free(s.bits);
	free(bg.bits);
}

static void gimme_tests(const Course *c)
{
	static const uint8_t players[] = { 1, 2, 2, 4, 1 };
	static Play p;
	Round r, back;
	RenderView next;
	uint8_t mode, holeout, buf[RULES_SAVE_MAX];
	const Hole *h = &holes[0].h;
	vec2 pin = h->pins[0], stop = { pin.x + YD(0.6), pin.y };

	for (mode = MODE_STROKE; mode <= MODE_TOURN; mode++) {
		for (holeout = 0; holeout <= 1; holeout++) {
			rules_init(&r, "Par386 Test", players[mode], c->hole_count, c->par, 9);
			rules_mode(&r, mode, NULL, 0);
			r.holeout = holeout;
			memset(&p, 0, sizeof p);
			play_start(&p, h, &holes[0].g, &r);
			p.state = PS_RESULT;
			p.have_shot = 1;
			p.shot.result = SHOT_OK;
			p.shot.stop = stop;
			p.shot.lie = LIE_GREEN;
			p.stroke = 3;
			CHECK(play_next_view(&p, &next) == holeout);
			play_settle(&p);
			CHECK(rules_unpack(&back, buf, rules_pack(&r, buf)) == 0 && back.holeout == holeout);
			CHECK(back.pl[0].score[0] == (holeout ? 0 : 4));
			if (holeout) {
				CHECK(back.pl[0].ball.x == stop.x && back.pl[0].ball.y == stop.y && back.pl[0].taken == 3 && !back.pl[0].holed);
				memset(&p, 0, sizeof p);
				play_start(&p, h, &holes[0].g, &back);
				CHECK(!back.turn || back.players > 1);
				if (!back.turn) CHECK(p.club == CLUB_PT && p.stroke == 4 && p.rv.ball.x == stop.x);
			}
		}
	}
}

static void stats_tests(const Course *c)
{
	static Play p;
	Round r, back;
	RoundPlayer *a = &r.pl[0];
	uint8_t buf[RULES_SAVE_MAX];
	const Hole *h = &holes[0].h;

	rules_init(&r, "Par386 Test", 1, c->hole_count, c->par, 9);
	play_start(&p, h, &holes[0].g, &r);
	p.state = PS_FLY;
	p.have_shot = 1;
	p.shot.result = SHOT_OK;
	p.shot.stop = vec2_make(YD(80), YD(220));
	p.shot.lie = LIE_FAIRWAY;
	play_settle(&p);
	play_settle(&p);
	CHECK(a->stats.fairways == 1 && a->stats.drives == 1 && a->taken == 1);
	CHECK(rules_unpack(&back, buf, rules_pack(&r, buf)) == 0 && back.stats_available &&
	      memcmp(&back.pl[0].stats, &a->stats, sizeof a->stats) == 0);
	play_start(&p, h, &holes[0].g, &back);
	p.state = PS_FLY;
	p.have_shot = 1;
	p.shot.result = SHOT_OK;
	p.shot.stop = vec2_make(h->pins[0].x + YD(0.6), h->pins[0].y);
	p.shot.lie = LIE_GREEN;
	play_settle(&p);
	CHECK(back.pl[0].stats.greens == 1 && back.pl[0].stats.putts == 1 &&
	      back.pl[0].score[0] == 3 && !back.pl[0].stats.green_hit);

	rules_stat_shot(&r, 0, LIE_FAIRWAY, LIE_ROUGH, SHOT_WATER, 0);
	rules_stat_shot(&r, 0, LIE_ROUGH, LIE_ROUGH, SHOT_OOB, 0);
	rules_stat_shot(&r, 0, LIE_FRINGE, LIE_GREEN, SHOT_OK, 0);
	rules_stat_shot(&r, 0, LIE_GREEN, LIE_GREEN, SHOT_HOLED, 0);
	CHECK(a->stats.putts == 1 && a->stats.greens == 1 && a->stats.penalties == 2);
	rules_mode(&r, MODE_GREEN, NULL, 0);
	rules_stat_shot(&r, 0, LIE_GREEN, LIE_GREEN, SHOT_HOLED, 0);
	CHECK(a->stats.putts == 1);
	rules_mode(&r, MODE_TOURN, NULL, 0);
	r.tour_stage = TOUR_BREAK;
	rules_tour_next(&r);
	CHECK(r.stats_available && !a->stats.putts && !a->stats.drives && !a->stats.greens && !a->stats.penalties);
}

static void record_tests(void)
{
	Records rs, back;
	Round r;
	uint8_t pars[3] = { 4, 3, 5 }, buf[REC_SAVE_MAX + 4], k;
	static const uint8_t cpu[2] = { CPU_HUMAN, CPU_PRO };
	static const uint8_t s1[2] = { 5, 3 }, s2[2] = { 3, 3 }, s3[2] = { 5, 4 };
	uint16_t len;
	const CourseRecord *c;

	rec_init(&rs);
	rules_init(&r, "Par386 Test", 2, 3, pars, 1);
	rules_mode(&r, MODE_STROKE, cpu, 0);
	hole_scores(&r, s1);
	hole_scores(&r, s2);
	/* not finished: nothing */
	CHECK(rec_round(&rs, &r, 0) == 0 && !rec_find(&rs, "Par386 Test", 0, SPAN_FULL));
	hole_scores(&r, s3);
	/* the person only, not the computer golfer, though they scored lower */
	CHECK(rec_round(&rs, &r, 0) == 1);
	c = rec_find(&rs, "Par386 Test", 0, SPAN_FULL);
	CHECK(c && c->strokes == 13 && c->to_par == 1 && c->holes == 3 && strcmp(c->name, "Player 1") == 0);
	CHECK(!rec_find(&rs, "Par386 Test", 1, SPAN_FULL) && !rec_find(&rs, "Par386 Other", 0, SPAN_FULL));
	/* the same score again isn't a new one; a better one is */
	CHECK(rec_round(&rs, &r, 0) == 0);
	r.pl[0].score[0] = 4;
	CHECK(rec_round(&rs, &r, 0) == 1 && rec_find(&rs, "Par386 Test", 0, SPAN_FULL)->strokes == 12);
	r.pl[0].score[0] = 6;
	CHECK(rec_round(&rs, &r, 0) == 0 && rec_find(&rs, "Par386 Test", 0, SPAN_FULL)->strokes == 12);
	/* another tee keeps its own */
	CHECK(rec_round(&rs, &r, 1) == 1 && rs.n == 2);
	/* practice sets none */
	r.mode = MODE_RANGE;
	r.players = 1;
	CHECK(rec_round(&rs, &r, 2) == 0);
	r.mode = MODE_STROKE;
	/* full: a new course replaces the oldest */
	for (k = 0; k < REC_MAX + 2; k++) {
		snprintf(r.course, sizeof r.course, "Par386 C%d", k);
		rec_round(&rs, &r, 0);
	}
	CHECK(rs.n == REC_MAX && !rec_find(&rs, "Par386 Test", 0, SPAN_FULL) && !rec_find(&rs, "Par386 C1", 0, SPAN_FULL) && rec_find(&rs, "Par386 C2", 0, SPAN_FULL) && rec_find(&rs, "Par386 C13", 0, SPAN_FULL) && rec_find(&rs, "Par386 C12", 0, SPAN_FULL));
	len = rec_pack(&rs, buf);
	CHECK(len == 4 + REC_MAX * REC_ITEM && len <= REC_SAVE_MAX);
	CHECK(rec_unpack(&back, buf, len) == 0 && memcmp(&back, &rs, sizeof rs) == 0);
	CHECK(rec_unpack(&back, buf, len - 1) == -1 && back.n == 0);
	buf[1] = REC_VERSION + 1;
	CHECK(rec_unpack(&back, buf, len) == -1);
	buf[1] = REC_VERSION;
	buf[3 + RULES_COURSE_MAX] = 'x';
	CHECK(rec_unpack(&back, buf, len) == -1);
	rec_init(&rs);
	CHECK(rec_unpack(&back, buf, rec_pack(&rs, buf)) == 0 && back.n == 0);
}

static void event_tests(void)
{
	static const uint8_t pars[3] = { 4, 3, 5 };
	static const uint8_t sc[2] = { 1, 1 };
	Records rs, back;
	Round r, saved;
	uint8_t buf[REC_SAVE_MAX], round[RULES_SAVE_MAX], h;
	uint16_t len;

	CHECK(strcmp(rec_event_name("Auld Kirk"), "Kirk Open") == 0);
	CHECK(strcmp(rec_event_name("Par386 Falesia Grove"), "Falesia Cup") == 0);
	CHECK(strcmp(rec_event_name("Par386 Add-on"), "Club Open") == 0);
	rec_init(&rs);
	rules_init(&r, "Par386 Auld Kirk", 2, 3, pars, 1);
	rules_mode(&r, MODE_TOURN, NULL, 0);
	for (h = 0; h < r.holes; h++) hole_scores(&r, sc);
	CHECK(r.tour_stage == TOUR_BREAK && !rec_event(&rs, &r));
	rules_tour_next(&r);
	hole_scores(&r, sc);
	CHECK(!rec_event(&rs, &r));
	len = rules_pack(&r, round);
	CHECK(rules_unpack(&saved, round, len) == 0);
	for (h = 1; h < saved.holes; h++) hole_scores(&saved, sc);
	CHECK(rec_event(&rs, &saved) && rec_best_finish(&rs, saved.course) == (EVENT_TIED | 1));
	CHECK(!rec_event(&rs, &saved));
	saved.tour_stage = TOUR_SINGLE;
	CHECK(!rec_event(&rs, &saved));
	saved.tour_stage = TOUR_SECOND;
	saved.pl[1].score[0]++;
	CHECK(rec_event(&rs, &saved) && rec_best_finish(&rs, saved.course) == 1);
	CHECK(!rec_event(&rs, &saved));
	CHECK(rec_best_finish(&rs, "Par386 Carrig Head") == 0);
	CHECK(rec_round(&rs, &saved, 0) > 0 && rs.events == 1);
	len = rec_pack(&rs, buf);
	CHECK(rec_unpack(&back, buf, len) == 0 && memcmp(&rs, &back, sizeof rs) == 0);
	buf[len - 1] = EVENT_TIED;
	CHECK(rec_unpack(&back, buf, len) == -1);
	buf[1] = 2;
	CHECK(rec_unpack(&back, buf, 3 + rs.n * REC_ITEM) == 0 && back.n == rs.n && !back.events);
}

static void nine_tests(void)
{
	static const uint8_t pars[18] = { 4, 3, 5, 4, 3, 5, 4, 4, 4, 5, 4, 3, 5, 4, 3, 4, 4, 4 };
	Round r, q;
	Course c = {0};
	Records rs, saved;
	uint8_t buf[REC_SAVE_MAX], span, mode, h, i;
	uint16_t len;
	Surface s;
	const CourseRecord *record;

	c.hole_count = 18;
	memcpy(c.par, pars, sizeof pars);
	rec_init(&rs);
	for (span = SPAN_FULL; span < SPAN_COUNT; span++) {
		rules_init(&r, "Par386 Example", 1, c.hole_count, c.par, 1);
		rules_span(&r, span);
		for (h = 0; h < r.holes; h++) {
			rules_hole_out(&r, 0, r.pars[h]);
			rules_advance(&r);
		}
		CHECK(rec_round(&rs, &r, 0) == 1);
	}
	CHECK(rs.n == 3);
	record = rec_find(&rs, "Par386 Example", 0, SPAN_FULL);
	CHECK(record && record->holes == 18);
	record = rec_find(&rs, "Par386 Example", 0, SPAN_FRONT);
	CHECK(record && record->holes == 9);
	record = rec_find(&rs, "Par386 Example", 0, SPAN_BACK);
	CHECK(record && record->holes == 9);
	CHECK(!rec_find(&rs, "Par386 Example", 1, SPAN_BACK));
	len = rec_pack(&rs, buf);
	CHECK(rec_unpack(&saved, buf, len) == 0 && memcmp(&rs, &saved, sizeof rs) == 0);
	buf[1] = 1;
	buf[2] = 1;
	CHECK(rec_unpack(&saved, buf, (uint32_t)(3 + REC_ITEM - 1)) == 0 && saved.n == 1 &&
	      saved.rec[0].span == SPAN_FULL && saved.rec[0].holes == 18);

	/* Scores stay round-relative and the back nine starts on physical hole 10. */
	for (mode = MODE_STROKE; mode <= MODE_BEST; mode++) {
		for (span = SPAN_FRONT; span < SPAN_COUNT; span++) {
			uint8_t first = (uint8_t)(span == SPAN_BACK ? 9 : 0);
			rules_init(&r, "Par386 Example", mode == MODE_BEST ? 4 : 2, c.hole_count, c.par, 1);
			rules_mode(&r, mode, NULL, 0);
			rules_span(&r, span);
			CHECK(r.hole == 0 && r.holes == 9 && r.span == span && rules_course_matches(&r, &c));
			CHECK(rules_hole_number(&r, r.hole) == first + 1);
			for (h = 0; h < r.holes; h++) {
				CHECK(!r.over && r.hole == h && rules_hole_number(&r, h) == first + h + 1);
				for (i = 0; i < r.players; i++) rules_hole_out(&r, i, r.pars[h]);
				rules_advance(&r);
				if (h == 3) {
					len = rules_pack(&r, buf);
					CHECK(rules_unpack(&q, buf, len) == 0 && memcmp(&q, &r, sizeof r) == 0);
					CHECK(rules_hole_number(&q, q.hole) == first + 5 && rules_course_matches(&q, &c));
					c.par[first]++;
					CHECK(!rules_course_matches(&q, &c));
					c.par[first]--;
					buf[len - 1] = SPAN_COUNT;
					CHECK(rules_unpack(&q, buf, len) == -1);
				}
			}
			CHECK(r.over && rules_played(&r, 0, 0, r.holes) == 9 && rules_to_par(&r, 0) == 0);
		}
	}
	for (i = 1; i <= 2; i++) {
		surface(&s, i);
		card_draw(&s, &r, CARD_FINAL, 0, 0);
		save(&s, "back-nine-card");
		free(s.bits);
	}
	rules_init(&r, "Par386 Example", 1, c.hole_count, c.par, 1);
	rules_mode(&r, MODE_TOURN, NULL, 0);
	rules_span(&r, SPAN_FRONT);
	CHECK(r.span == SPAN_FULL && r.holes == 18);
	rules_init(&r, "Par386 Nine", 1, 9, c.par, 1);
	rules_span(&r, SPAN_BACK);
	CHECK(r.span == SPAN_FULL && r.holes == 9 && rules_hole_number(&r, 0) == 1);
	memcpy(c.par + 18, pars, sizeof pars);
	rules_init(&r, "Par386 Long", 1, 36, c.par, 1);
	rules_span(&r, SPAN_FRONT);
	CHECK(r.span == SPAN_FULL && r.holes == 36);
}

static void mode_tests(const Course *c)
{
	match_tests();
	skins_tests();
	tournament_tests();
	record_tests();
	event_tests();
	nine_tests();
	panel_tests(c);
	practice_tests(c);
	gimme_tests(c);
	stats_tests(c);
	cpu_start_save_test(c);
	cpu_tests(c);
}

/* ---- the clubhouse ---- */

static void draw_front(Surface *s, const Front *f)
{
	char name[64];
	uint16_t id = front_picture(f, s), k;

	gfx_fill(s, 0, 0, s->w, s->h, UIC_BLACK);
	for (k = 0; ; k++) {
		uint32_t len;
		uint8_t *d;
		snprintf(name, sizeof name, "Pic%d-%d.bin", s->depth, id + k);
		if (!(d = load_art(name, &len))) break;
		CHECK(front_picture_band(s, id, d, len) == 0);
		free(d);
	}
	CHECK(k > 0);
	front_draw(f, s);
	if ((id = front_preview(f)) != 0)
		for (k = 0; ; k++) {
			uint32_t len;
			uint8_t *d;
			snprintf(name, sizeof name, "Pic%d-%d.bin", s->depth, id + k);
			if (!(d = load_art(name, &len))) break;
			CHECK(front_preview_band(s, d, len) == 0);
			free(d);
		}
}

static void front_tests(void)
{
	char line[20];
	Front f;
	Surface s;
	uint8_t scale;
	static const char *names[2] = { "Auldkirk", "Test" };
	static const uint8_t scenery[2] = { 1, 0 };

	/* Tournament setup shows the event and the saved placing below the list. */
	{
		static const char *courses[] = { "Auld Kirk" };
		front_init(&f);
		front_courses(&f, courses, NULL, 1, 0);
		f.event_best[0] = EVENT_TIED | 2;
		front_clubhouse(&f, 0);
		CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_GAMES);
		f.menu.sel = MODE_TOURN;
		CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_PLAYERS);
		CHECK(f.menu.y + f.menu.count * f.menu.lineH <= 125);
		for (scale = 1; scale <= 2; scale++) {
			surface(&s, scale);
			draw_front(&s, &f);
			save(&s, "event-setup");
			free(s.bits);
		}
	}

	/* no save: Play a round goes to the games, then the players */
	front_init(&f);
	front_clubhouse(&f, 0);
	CHECK(f.menu.count == MAIN_QUIT + 1 && front_action(&f, 0) == MAIN_PLAY && front_action(&f, MAIN_PLAYER) == MAIN_PLAYER);
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_GAMES && f.menu.sel == 0 && f.menu.count == 6);
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_PLAYERS && f.mode == MODE_STROKE);
	CHECK(f.menu.sel == 5 && f.menu.count == 7);
	CHECK(strcmp(f.players_list, "P1  Human|P2  Off|P3  Off|P4  Off|Tees: Back|Tee off|Back") == 0);
	CHECK(f.menu.y + 6 * f.menu.lineH + UI_CHAR < 150);
	/* a slot goes round on select and right, back on left; the first is never off */
	f.menu.sel = 1;
	CHECK(front_key(&f, KEY_SELECT) == (FR_REDRAW | FR_LINES) && f.slot[1] == CPU_HUMAN && f.menu.sel == 1);
	CHECK(front_key(&f, KEY_RIGHT) == (FR_REDRAW | FR_LINES) && f.slot[1] == CPU_EASY);
	CHECK(front_key(&f, KEY_RIGHT) && f.slot[1] == CPU_FAIR);
	f.menu.sel = 0;
	CHECK(front_key(&f, KEY_LEFT) && f.slot[0] == CPU_PRO);
	CHECK(front_key(&f, KEY_RIGHT) && f.slot[0] == CPU_HUMAN);
	f.menu.sel = 3;
	CHECK(front_key(&f, KEY_LEFT) && f.slot[3] == CPU_PRO);
	CHECK(front_key(&f, KEY_RIGHT) && f.slot[3] == SLOT_OFF);
	f.menu.sel = 2;
	CHECK(front_key(&f, KEY_LEFT) && f.slot[2] == CPU_PRO);
	CHECK(strcmp(f.players_list, "P1  Human|P2  CPU Fair|P3  CPU Pro|P4  Off|Tees: Back|Tee off|Back") == 0);
	/* the tee set goes round on select, right and left; a course not read yet has three */
	f.menu.sel = 4;
	CHECK(front_key(&f, KEY_SELECT) == (FR_REDRAW | FR_LINES) && f.tee == 1 && f.menu.sel == 4);
	CHECK(strcmp(f.players_list, "P1  Human|P2  CPU Fair|P3  CPU Pro|P4  Off|Tees: Middle|Tee off|Back") == 0);
	CHECK(front_key(&f, KEY_RIGHT) && f.tee == 2);
	CHECK(front_key(&f, KEY_RIGHT) && f.tee == 0);
	CHECK(front_key(&f, KEY_LEFT) && f.tee == 2);
	CHECK(front_key(&f, KEY_LEFT) && f.tee == 1);
	/* a tap on the tee line from Tee off moves the highlight: both lines redraw */
	f.menu.sel = 5;
	CHECK(front_tap(&f, 60, f.menu.y + 4 * f.menu.lineH + 2) == (FR_REDRAW | FR_LINES) && f.tee == 2);
	CHECK(f.menu.sel == 4 && f.oldSel == 5);
	CHECK(front_key(&f, KEY_RIGHT) && f.tee == 0);
	CHECK(front_key(&f, KEY_RIGHT) && f.tee == 1);
	/* Tee off: the slots not off, in order */
	f.menu.sel = 5;
	CHECK(front_key(&f, KEY_SELECT) == FR_PLAY && f.players == 3 && f.mode == MODE_STROKE);
	CHECK(f.cpu[0] == CPU_HUMAN && f.cpu[1] == CPU_FAIR && f.cpu[2] == CPU_PRO);
	CHECK(rules_mode_ok(f.mode, f.players));
	/* Back goes back to the games, and from there the clubhouse */
	f.menu.sel = 6;
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_GAMES && f.menu.sel == MODE_STROKE);
	CHECK(front_key(&f, KEY_BACK) == FR_REDRAW && f.screen == SCR_MENU && f.menu.count == MAIN_QUIT + 1);

	/* match play: two slots, neither off */
	f.slot[1] = SLOT_OFF;
	front_key(&f, KEY_SELECT);
	f.menu.sel = MODE_MATCH;
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_PLAYERS && f.mode == MODE_MATCH);
	CHECK(strcmp(f.players_list, "P1  Human|P2  CPU Fair|Tees: Middle|Tee off|Back") == 0 && f.menu.sel == 3);
	f.menu.sel = 1;
	CHECK(front_key(&f, KEY_LEFT) && f.slot[1] == CPU_EASY);
	CHECK(front_key(&f, KEY_LEFT) && f.slot[1] == CPU_HUMAN);
	CHECK(front_key(&f, KEY_LEFT) && f.slot[1] == CPU_PRO);
	f.menu.sel = 3;
	CHECK(front_key(&f, KEY_SELECT) == FR_PLAY && f.players == 2 && f.cpu[1] == CPU_PRO && rules_mode_ok(f.mode, f.players));
	/* best ball: four, none off; skins: at least two; a tap on Tee off */
	CHECK(front_key(&f, KEY_BACK) == FR_REDRAW && f.screen == SCR_GAMES && f.menu.sel == MODE_MATCH);
	f.menu.sel = MODE_BEST;
	front_key(&f, KEY_SELECT);
	CHECK(f.slot[1] == CPU_PRO && f.slot[2] == CPU_PRO && f.slot[3] == CPU_FAIR);
	CHECK(front_tap(&f, 60, f.menu.y + 5 * f.menu.lineH + 2) & FR_PLAY);
	CHECK(f.players == 4 && rules_mode_ok(MODE_BEST, f.players));
	front_key(&f, KEY_BACK);
	f.menu.sel = MODE_SKINS;
	f.slot[2] = f.slot[3] = SLOT_OFF;
	front_key(&f, KEY_SELECT);
	f.menu.sel = 1;
	CHECK(front_key(&f, KEY_RIGHT) && f.slot[1] == CPU_HUMAN);
	f.menu.sel = 5;
	CHECK(front_key(&f, KEY_SELECT) == FR_PLAY && f.players == 2 && f.mode == MODE_SKINS);

	/* Practice: the range and the green, one player */
	front_clubhouse(&f, 0);
	f.menu.sel = MAIN_PRACTICE;
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_PRACTICE && f.menu.count == 3);
	CHECK(front_key(&f, KEY_SELECT) == FR_PLAY && f.mode == MODE_RANGE && f.players == 1 && f.cpu[0] == CPU_HUMAN);
	f.menu.sel = 1;
	CHECK(front_key(&f, KEY_SELECT) == FR_PLAY && f.mode == MODE_GREEN);
	f.menu.sel = 2;
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_MENU && f.menu.sel == MAIN_PRACTICE);

	/* with a save: Continue round first, the rest a line down */
	front_clubhouse(&f, 1);
	CHECK(f.menu.count == MAIN_QUIT + 2 && front_action(&f, 0) == MAIN_CONTINUE && front_action(&f, 1) == MAIN_PLAY &&
	      front_action(&f, MAIN_PLAYER + 1) == MAIN_PLAYER && front_action(&f, MAIN_QUIT + 1) == MAIN_QUIT);
	CHECK(f.menu.y + (MAIN_QUIT + 1) * f.menu.lineH + UI_CHAR < 150);
	CHECK(front_key(&f, KEY_SELECT) == FR_CONTINUE);
	f.menu.sel = MAIN_QUIT + 1;
	CHECK(front_key(&f, KEY_SELECT) == FR_QUIT);
	/* a new round asks first: Cancel keeps the save, OK goes on to the games */
	f.menu.sel = 1;
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.dialog == DLG_NEW_ROUND && f.dlg.count == 2);
	CHECK(front_key(&f, KEY_RIGHT) == FR_REDRAW && f.dlg.sel == 1);
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && !f.dialog && f.screen == SCR_MENU);
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.dialog == DLG_NEW_ROUND);
	CHECK(front_tap(&f, f.dlg.btn[0].x + 4, f.dlg.btn[0].y + 4) == FR_REDRAW && f.screen == SCR_GAMES);
	/* and practice asks too, then goes to the practice screen */
	front_clubhouse(&f, 1);
	f.menu.sel = 2;
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.dialog == DLG_NEW_ROUND);
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_PRACTICE);

	/* seven courses: pages of 4 with More and Back, round to the first */
	{
		static const char *seven[7] = { "A1", "B2", "C3", "D4", "E5", "F6", "G7" };
		front_clubhouse(&f, 0);
		front_courses(&f, seven, NULL, 7, 0);
		f.menu.sel = MAIN_COURSES;
		CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_COURSES);
		CHECK(strcmp(f.course_list, "A1|B2|C3|D4|More|Back") == 0 && f.menu.sel == 0);
		f.menu.sel = 4;
		CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && strcmp(f.course_list, "E5|F6|G7|More|Back") == 0 && f.menu.sel == 3);
		f.menu.sel = 1;
		CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_MENU && f.course == 5);
		f.menu.sel = MAIN_COURSES;
		front_key(&f, KEY_SELECT);
		CHECK(f.page == 1 && f.menu.sel == 1 && strcmp(f.course_list, "E5|F6|G7|More|Back") == 0);
		f.menu.sel = 3;
		CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && strcmp(f.course_list, "A1|B2|C3|D4|More|Back") == 0);
		CHECK(front_key(&f, KEY_BACK) == FR_REDRAW && f.screen == SCR_MENU && f.course == 5);
		/* five fit with Back */
		front_courses(&f, seven, NULL, 5, 0);
		f.menu.sel = MAIN_COURSES;
		front_key(&f, KEY_SELECT);
		CHECK(strcmp(f.course_list, "A1|B2|C3|D4|E5|Back") == 0);
		f.menu.sel = 4;
		CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_MENU && f.course == 4);
		f.course = 0;
	}

	/* course records under the preview */
	front_courses(&f, names, scenery, 2, 0);
	f.tee = 0;
	front_record(&f, 0, 0, SPAN_FULL, 71, -1);
	front_record(&f, 1, 0, SPAN_FULL, 0, 0);
	front_record_line(&f, 0, line);
	CHECK(strcmp(line, "Record 71 (-1)") == 0);
	front_record_line(&f, 1, line);
	CHECK(line[0] == 0);
	front_record(&f, 1, 0, SPAN_FULL, 13, 1);
	front_record_line(&f, 1, line);
	CHECK(strcmp(line, "Record 13 (+1)") == 0);
	front_record(&f, 1, 0, SPAN_FULL, 12, 0);
	front_record_line(&f, 1, line);
	CHECK(strcmp(line, "Record 12 (E)") == 0);
	front_record(&f, 1, 0, SPAN_FULL, 0, 0);
	/* with the course's tee sets known: the picked set's record under its name */
	{
		Course tc;
		memset(&tc, 0, sizeof tc);
		tc.tee_count = 3;
		strcpy(tc.tee_names[0], "Medal");
		strcpy(tc.tee_names[1], "Club");
		strcpy(tc.tee_names[2], "Forwardmost");
		front_course_tees(&f, 0, &tc);
		front_record(&f, 0, 2, SPAN_FULL, 101, 29);
		front_record_line(&f, 0, line);
		CHECK(strcmp(line, "Medal 71 (-1)") == 0);
		f.tee = 1;
		front_record_line(&f, 0, line);
		CHECK(line[0] == 0);
		f.tee = 2;
		front_record_line(&f, 0, line);
		CHECK(strcmp(line, "Forwar 101 (+29)") == 0);
		f.tee = 3;
		front_record_line(&f, 0, line);
		CHECK(strcmp(line, "Forwar 101 (+29)") == 0);
		f.course = 0;
		CHECK(front_tee_count(&f) == 3 && strcmp(front_tee_name(&f, 1), "Club") == 0);
		front_course_tees(&f, 0, NULL);
		CHECK(front_tee_count(&f) == 3 && strcmp(front_tee_name(&f, 1), "Middle") == 0);
		front_course_tees(&f, 0, &tc);
		f.tee = 0;
	}

	for (scale = 1; scale <= 2; scale++) {
		surface(&s, scale);
		front_clubhouse(&f, 1);
		draw_front(&s, &f);
		save(&s, "clubhouse-continue");
		f.menu.sel = 1;
		front_key(&f, KEY_SELECT);
		draw_front(&s, &f);
		save(&s, "new-round");
		front_key(&f, KEY_SELECT);
		draw_front(&s, &f);
		save(&s, "m12-games");
		f.menu.sel = MODE_STROKE;
		f.slot[1] = CPU_FAIR; f.slot[2] = CPU_PRO; f.slot[3] = SLOT_OFF;
		front_key(&f, KEY_SELECT);
		draw_front(&s, &f);
		save(&s, "m12-players");
		front_clubhouse(&f, 0);
		f.menu.sel = MAIN_PRACTICE;
		front_key(&f, KEY_SELECT);
		draw_front(&s, &f);
		save(&s, "m12-practice");
		front_clubhouse(&f, 0);
		f.menu.sel = MAIN_COURSES;
		front_key(&f, KEY_SELECT);
		draw_front(&s, &f);
		save(&s, "m12-courses-record");
		front_key(&f, KEY_DOWN);
		draw_front(&s, &f);
		save(&s, "m12-courses-none");
		front_clubhouse(&f, 0);
		f.menu.sel = MAIN_SETTINGS;
		front_key(&f, KEY_SELECT);
		draw_front(&s, &f);
		save(&s, "m12-settings");
		free(s.bits);
	}

	/* Known 18-hole courses offer both nines before Tee off. */
	{
		Course tc = {0};
		tc.tee_count = 1;
		tc.hole_count = 18;
		strcpy(tc.tee_names[0], "Medal");
		front_course_tees(&f, 0, &tc);
		front_record(&f, 0, 0, SPAN_BACK, 38, 2);
		for (scale = 1; scale <= 2; scale++) {
			f.course = 0;
			f.span = SPAN_FULL;
			f.mode = MODE_STROKE;
			front_clubhouse(&f, 0);
			f.menu.sel = MAIN_PLAY;
			CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_GAMES);
			CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_PLAYERS && f.menu.sel == 6);
			CHECK(front_tap(&f, 80, (int16_t)(f.menu.y + 5 * f.menu.lineH + 4)) == (FR_REDRAW | FR_LINES));
			CHECK(f.span == SPAN_FRONT && f.menu.sel == 5);
			CHECK(front_key(&f, KEY_RIGHT) == (FR_REDRAW | FR_LINES) && f.span == SPAN_BACK);
			front_record_line(&f, 0, line);
			CHECK(strcmp(line, "Back 9 38 (+2)") == 0);
			surface(&s, scale);
			draw_front(&s, &f);
			save(&s, "back-nine-setup");
			free(s.bits);
			front_key(&f, KEY_DOWN);
			CHECK(front_key(&f, KEY_SELECT) == FR_PLAY && f.span == SPAN_BACK);
			CHECK(front_key(&f, KEY_BACK) == FR_REDRAW && f.screen == SCR_GAMES);
			f.menu.sel = MODE_TOURN;
			CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.menu.sel == 5 && !strstr(f.players_list, "Holes:"));
		}
	}
}

/* Hole 1 of the test course alone with the wind of PLAY_SEED=3, as
 * tools/replay/hole1.replay plays it on the Palm: the sounds each shot
 * makes, in order, and the tune at the end. Then the menu clicks. */
static void sound_tests(void)
{
	static Play p;
	Surface s, bg;
	Front f;
	uint32_t t = 0;
	uint16_t k, start, swings = 0, clicks = 0, hits = 0, putts = 0, lands = 0, cups = 0, tunes = 0;
	int shots = 0;
	static const char *const names[SND_COUNT] = {
		"none", "swing", "click", "wood", "iron", "putt", "land-fairway", "land-rough", "land-sand",
		"splash", "cup", "oob", "holed", "birdie", "bogey", "menu-move", "menu-select",
		"land-path", "lip", "mishit", "club-change", "ace"
	};

	surface(&s, 1);
	surface(&bg, 1);
	snd_log_n = 0;
	while (snd_take() != SND_NONE) {}
	play_init(&p, &holes[0].h, &holes[0].g, 3);
	CHECK(snd_ambient_on() && snd_log_n == 1 && snd_log[0] == 0x81);
	play_frame(&p, &s, &bg, t);
	while (p.state != PS_DONE && shots < 12) {
		start = snd_log_n;
		shot(&p, &s, &bg, &t);
		shots++;
		/* the swing starts with the birds off from the tee */
		CHECK(snd_log[start] == 0x80 || snd_log[start] == SND_SWING);
	}
	CHECK(p.state == PS_DONE);
	printf("sound: hole 1 in %d:", p.score);
	for (k = 0; k < snd_log_n; k++) {
		uint8_t v = snd_log[k];
		printf(" %s", v >= 0x80 ? (v & 1 ? "birds-on" : "birds-off") : v < SND_COUNT ? names[v] : "?");
		switch (v) {
		case SND_SWING: swings++; break;
		case SND_CLICK: clicks++; break;
		case SND_HIT_WOOD: case SND_HIT_IRON: hits++; break;
		case SND_HIT_PUTT: hits++; putts++; break;
		case SND_LAND_FAIRWAY: case SND_LAND_ROUGH: case SND_LAND_SAND: case SND_LAND_PATH:
		case SND_SPLASH: lands++; break;
		case SND_CUP: cups++; break;
		case SND_HOLED: case SND_BIRDIE: case SND_BOGEY: case SND_ACE: tunes++; break;
		}
		/* a putt has no power click before it, a swing does */
		if (v == SND_HIT_PUTT) CHECK(k > 0 && snd_log[k - 1] == SND_SWING);
		if (v == SND_HIT_WOOD || v == SND_HIT_IRON) CHECK(k > 0 && snd_log[k - 1] == SND_CLICK);
	}
	printf("\n");
	CHECK(swings == shots && hits == shots && clicks == shots - putts && putts >= 1);
	/* every full shot comes down once; the driver from the tee */
	CHECK(lands == shots - putts && snd_log[4] == SND_HIT_WOOD);
	CHECK(tunes == 1 && snd_log[snd_log_n - 1] == (p.score < 4 ? SND_BIRDIE : p.score == 4 ? SND_HOLED : SND_BOGEY));
	CHECK(cups <= 1);
	/* the queue holds the newest SND_QUEUE */
	k = 0;
	while (snd_take() != SND_NONE) k++;
	CHECK(k == SND_QUEUE || k == snd_log_n);

	/* Change the club and shot shape, then let the snap marker run out. */
	play_init(&p, &holes[0].h, &holes[0].g, 3);
	snd_log_n = 0;
	play_key(&p, KEY_DOWN, t);
	play_key(&p, KEY_SHAPE, t);
	CHECK(snd_log_n == 2 && snd_log[0] == SND_CLUB_CHANGE && snd_log[1] == SND_CLUB_CHANGE);
	play_key(&p, KEY_SELECT, t);
	play_key(&p, KEY_SELECT, t + MTR_UP_MS / 2);
	t += MTR_UP_MS + MTR_DOWN_MS;
	play_frame(&p, &s, &bg, t);
	CHECK(p.state == PS_FLY && p.mishit && snd_log[snd_log_n - 1] == SND_MISHIT);

	/* putts from 7 yd at a range of powers until one drops: the cup, then the
	 * birdie tune (2 on a par 4), with no landing sound for the putter */
	{
		Round r;
		uint8_t pars[3] = { 4, 3, 5 };
		uint32_t ms = 0;
		int holed = 0, left;
		/* it breaks right: aimed a degree or more left */
		for (left = 0; left < 6 && !holed; left++)
		for (ms = 700; ms <= 1000 && !holed; ms += 10) {
			vec2 pin = holes[0].h.pins[0];
			rules_init(&r, "Par386 Test", 1, 3, pars, 3);
			r.pl[0].taken = 1;
			r.pl[0].ball = vec2_make(pin.x - (2L << 16), pin.y - (7L << 16));
			r.pl[0].lie = LIE_GREEN;
			play_start(&p, &holes[0].h, &holes[0].g, &r);
			CHECK(p.club == CLUB_PT && !snd_ambient_on());
			snd_log_n = 0;
			t += 1000;
			for (k = 0; k < left; k++) play_key(&p, KEY_LEFT, t);
			play_key(&p, KEY_SELECT, t);
			t += ms;
			play_tap(&p, 80, 60, t);
			run(&p, &s, &bg, &t, PS_FLY);
			run(&p, &s, &bg, &t, PS_RESULT);
			if (p.shot.result != SHOT_HOLED) continue;
			holed = 1;
			CHECK(snd_log_n == 4 && snd_log[0] == SND_SWING && snd_log[1] == SND_HIT_PUTT && snd_log[2] == SND_CUP &&
			      snd_log[3] == SND_BIRDIE);
		}
		printf("sound: holed a 7 yd putt aimed %d degrees left at %u ms\n", left - 1, (unsigned)(ms - 10));
		CHECK(holed);
		if (holed) {
			/* Play the same stroke as the first shot and advance during the cup sound. */
			vec2 pin = holes[0].h.pins[0];
			rules_init(&r, "Par386 Test", 1, 3, pars, 3);
			r.pl[0].taken = 1;
			r.pl[0].ball = vec2_make(pin.x - (2L << 16), pin.y - (7L << 16));
			r.pl[0].lie = LIE_GREEN;
			play_start(&p, &holes[0].h, &holes[0].g, &r);
			p.stroke = 1;
			snd_log_n = 0;
			t += 1000;
			for (k = 1; k < left; k++) play_key(&p, KEY_LEFT, t);
			play_key(&p, KEY_SELECT, t);
			t += ms - 10;
			play_tap(&p, 80, 60, t);
			run(&p, &s, &bg, &t, PS_FLY);
			play_key(&p, KEY_SELECT, t);
			play_frame(&p, &s, &bg, t);
			CHECK(p.state == PS_DONE && p.score == 1);
			CHECK(snd_log_n == 4 && snd_log[2] == SND_CUP && snd_log[3] == SND_ACE);
		}
		while (snd_take() != SND_NONE) {}
	}

	/* menus: a move, a pick, the settings screen's sound line going round */
	snd_log_n = 0;
	front_init(&f);
	front_clubhouse(&f, 0);
	CHECK(front_key(&f, KEY_DOWN) & FR_LINES);
	CHECK(snd_log_n == 1 && snd_log[0] == SND_MENU_MOVE);
	f.menu.sel = MAIN_SETTINGS;
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_SETTINGS && f.menu.count == 5);
	CHECK(snd_log_n == 2 && snd_log[1] == SND_MENU_SELECT);
	CHECK(strcmp(f.settings_list, "Sound: On|CPU: Quick|Shot view: Map|Gimmies: 2 ft|Back") == 0);
	draw_front(&s, &f);
	save(&s, "settings");
	CHECK(front_key(&f, KEY_SELECT) == (FR_REDRAW | FR_SOUND) && f.sound == SOUND_FX && f.menu.sel == 0);
	CHECK(strcmp(f.settings_list, "Sound: Effects|CPU: Quick|Shot view: Map|Gimmies: 2 ft|Back") == 0);
	draw_front(&s, &f);
	save(&s, "settings-fx");
	CHECK(front_key(&f, KEY_SELECT) == (FR_REDRAW | FR_SOUND) && f.sound == SOUND_OFF);
	CHECK(front_key(&f, KEY_SELECT) == (FR_REDRAW | FR_SOUND) && f.sound == SOUND_ON);
	/* the computer golfers' shots: quick or the full flight */
	CHECK(front_key(&f, KEY_DOWN) & FR_LINES);
	CHECK(front_key(&f, KEY_SELECT) == (FR_REDRAW | FR_CPU) && !f.quick && f.menu.sel == 1);
	CHECK(strcmp(f.settings_list, "Sound: On|CPU: Full|Shot view: Map|Gimmies: 2 ft|Back") == 0);
	CHECK(front_key(&f, KEY_SELECT) == (FR_REDRAW | FR_CPU) && f.quick);
	/* full shots on the map or in the scene */
	CHECK(front_key(&f, KEY_DOWN) & FR_LINES);
	CHECK(front_key(&f, KEY_SELECT) == (FR_REDRAW | FR_SHOT) && !f.shotmap && f.menu.sel == 2);
	CHECK(strcmp(f.settings_list, "Sound: On|CPU: Quick|Shot view: 3D|Gimmies: 2 ft|Back") == 0);
	draw_front(&s, &f);
	save(&s, "settings-3d");
	CHECK(front_key(&f, KEY_SELECT) == (FR_REDRAW | FR_SHOT) && f.shotmap);
	CHECK(front_key(&f, KEY_DOWN) & FR_LINES);
	CHECK(front_key(&f, KEY_SELECT) == (FR_REDRAW | FR_GIMMES) && f.holeout && f.menu.sel == 3);
	CHECK(strcmp(f.settings_list, "Sound: On|CPU: Quick|Shot view: Map|Hole out|Back") == 0);
	draw_front(&s, &f);
	save(&s, "short-game-hole-out");
	CHECK(front_key(&f, KEY_SELECT) == (FR_REDRAW | FR_GIMMES) && !f.holeout);
	CHECK(front_key(&f, KEY_DOWN) & FR_LINES);
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_MENU && f.menu.sel == MAIN_SETTINGS);
	front_key(&f, KEY_SELECT);
	CHECK(f.screen == SCR_SETTINGS && f.menu.sel == 0);
	CHECK(front_key(&f, KEY_BACK) == FR_REDRAW && f.screen == SCR_MENU);
	free(s.bits);
	free(bg.bits);
	while (snd_take() != SND_NONE) {}
}

/* Computer golfers round a whole course: Easy, Fair, Good and Pro in
 * stroke play, quick shots, over a few seeds with the course's own wind.
 * Prints each hole's mean score per level, shots in the water or out of
 * bounds and pick-ups (5 over par), and fails if a round doesn't finish
 * or a Good or Pro golfer picks up. */
static int cpu_course(const char *path, int rounds, int tee)
{
	static const uint8_t four[4] = { CPU_EASY, CPU_FAIR, CPU_GOOD, CPU_PRO };
	static const char *lv[4] = { "Easy", "Fair", "Good", "Pro" };
	static Play p;
	Course c;
	Round r;
	Surface s, bg;
	int k, i, seed, sum[4][COURSE_MAX_HOLES], wet[COURSE_MAX_HOLES], oob[COURSE_MAX_HOLES], up[4][COURSE_MAX_HOLES];
	int tot[4] = { 0 }, best[4], worst[4], par = 0;

	if (rounds < 1) { printf("FAIL rounds must be 1 or more\n"); return 1; }
	if (!load_course(path, &c)) { printf("FAIL can't load %s\n", path); return 1; }
	if (tee < 0 || tee >= c.tee_count) { printf("FAIL %s has tee sets 0 to %d\n", path, c.tee_count - 1); return 1; }
	for (k = 0; k < c.hole_count; k++) {
		holes[k].h.turn = (uint16_t)(c.turn[k] << 8);
		holes[k].h.wind = c.wind;
		holes[k].h.green = c.green;
		hole_use_tee(&holes[k].h, (uint8_t)tee);
		par += c.par[k];
	}
	memset(sum, 0, sizeof sum);
	memset(up, 0, sizeof up);
	memset(wet, 0, sizeof wet);
	memset(oob, 0, sizeof oob);
	for (i = 0; i < 4; i++) { best[i] = 999; worst[i] = 0; }
	surface(&s, 1);
	surface(&bg, 1);
	for (seed = 1; seed <= rounds; seed++) {
		uint32_t t = 0;
		long n = 0;
		int lastc = -1, lasth = -1, lasts = -1, f;
		rules_init(&r, c.name, 4, c.hole_count, c.par, (uint32_t)seed * 7919);
		rules_mode(&r, MODE_STROKE, four, 0);
		r.tee = (uint8_t)tee;
		memset(&p, 0, sizeof p);
		p.quick = 1;
		play_start(&p, &holes[r.hole].h, &holes[r.hole].g, &r);
		while (!r.over && n < 2000000L) {
			n++;
			t += 20;
			if (p.have_shot && (p.cur != lastc || r.hole != lasth || p.stroke != lasts)) {
				lastc = p.cur;
				lasth = r.hole;
				lasts = p.stroke;
				if (p.shot.result == SHOT_WATER) wet[r.hole]++;
				if (p.shot.result == SHOT_OOB) oob[r.hole]++;
				/* CPU_TRACE=1 prints every shot of round 1, CPU_TRACE=wet every shot into the water */
				if (getenv("CPU_TRACE") && (strcmp(getenv("CPU_TRACE"), "wet") == 0 ? p.shot.result == SHOT_WATER : seed == 1))
					printf("  h%d %s s%d club %d from %.0f,%.0f land %.0f,%.0f (surf %d) stop %.0f,%.0f lie %d result %d\n",
					       r.hole + 1, lv[p.cur], p.stroke, p.club, p.shot.start.x / 65536.0, p.shot.start.y / 65536.0,
					       p.shot.land.x / 65536.0, p.shot.land.y / 65536.0, p.shot.land_surf,
					       p.shot.stop.x / 65536.0, p.shot.stop.y / 65536.0, p.shot.lie, p.shot.result);
			}
			f = p.view == PLAY_CARD ? play_key(&p, KEY_SELECT, t) : play_frame(&p, &s, &bg, t);
			if (f & PL_BACK) break;
			if (f & PL_NEXT) play_start(&p, &holes[r.hole].h, &holes[r.hole].g, &r);
		}
		CHECK(r.over);
		for (i = 0; i < 4; i++) {
			int st = rules_strokes(&r, (uint8_t)i, 0, r.holes);
			tot[i] += st;
			if (st < best[i]) best[i] = st;
			if (st > worst[i]) worst[i] = st;
			for (k = 0; k < c.hole_count; k++) {
				sum[i][k] += r.pl[i].score[k];
				if (r.pl[i].score[k] >= c.par[k] + 5) up[i][k]++;
			}
		}
	}
	/* a spread of tee shots on every hole, and on par 4s and 5s of shots
	 * from the aim point at the pin: those in the water are dropped on dry
	 * ground in bounds */
	printf("shots in the water, of 168 (7 clubs, 4 powers, 6 snaps) from the tee and 168 from the aim point,\n"
	       "and where they were dropped:\n");
	for (k = 0; k < c.hole_count * 2; k++) {
		static const uint8_t clubs[7] = { CLUB_DR, CLUB_3W, CLUB_3I, CLUB_5I, CLUB_7I, CLUB_9I, CLUB_SW };
		const Hole *h = &holes[k >> 1].h;
		int nw = 0, bad = 0, again = 0, ci, pi, si, from_aim = k & 1;
		double far = 0;
		if (from_aim && h->par == 3) continue;
		for (ci = 0; ci < 7; ci++)
			for (pi = 0; pi < 4; pi++)
				for (si = 0; si < 6; si++) {
					static Shot o;
					ShotIn in;
					uint8_t sf;
					memset(&in, 0, sizeof in);
					in.hole = h;
					in.grid = &holes[k >> 1].g;
					in.pos = from_aim ? h->aim : h->tees[h->tee].pos;
					in.pin = h->pins[0];
					in.aim = vec2_angle(vec2_sub(h->par == 3 || from_aim ? h->pins[0] : h->aim, in.pos));
					in.power = FIX_ONE * (4 + pi * 2) / 10;
					in.snap = (fixed)(si - 3) * (FIX_ONE / 5) + FIX_ONE / 10;
					in.club = clubs[ci];
					in.lie = from_aim ? LIE_FAIRWAY : LIE_TEE;
					ball_shot(&in, &o);
					if (o.result != SHOT_WATER) continue;
					nw++;
					sf = surface_at(&holes[k >> 1].g, o.stop.x, o.stop.y);
					if (sf == SURF_WATER || sf == SURF_OOB || o.stop.x < 0 || o.stop.y < 0 ||
					    o.stop.x > ((fixed)h->width << 16) || o.stop.y > ((fixed)h->length << 16)) bad++;
					if (o.stop.x == in.pos.x && o.stop.y == in.pos.y) {
						/* no dry ground back from the pin through where it went in: played again from where it was hit */
						again++;
					} else {
						double dx = (o.stop.x - o.land.x) / 65536.0, dy = (o.stop.y - o.land.y) / 65536.0;
						if (sqrt(dx * dx + dy * dy) > far) far = sqrt(dx * dx + dy * dy);
					}
				}
		if (nw) printf("  hole %2d %s: %3d in the water, dropped up to %.0f yd from where they landed, %d played again, %d on a bad spot\n",
		               (k >> 1) + 1, from_aim ? "aim" : "tee", nw, far, again, bad);
		CHECK(bad == 0);
	}
	printf("%s, %s tees, par %d, wind %d, green speed %d, %d rounds of 4 computer golfers\n", c.name, c.tee_names[tee], par,
	       c.wind ? c.wind : 10, c.green ? c.green : GREEN_USUAL, rounds);
	printf("hole par   easy  fair  good   pro  water oob  pick-ups (e f g p)\n");
	for (k = 0; k < c.hole_count; k++) {
		printf("%4d %3d ", k + 1, c.par[k]);
		for (i = 0; i < 4; i++) printf(" %5.2f", sum[i][k] / (double)rounds);
		printf("  %5d %3d  %d %d %d %d\n", wet[k], oob[k], up[0][k], up[1][k], up[2][k], up[3][k]);
		CHECK(up[2][k] == 0 && up[3][k] == 0);
	}
	for (i = 0; i < 4; i++)
		printf("%-4s mean %.1f (%+.1f), best %d, worst %d\n", lv[i], tot[i] / (double)rounds, tot[i] / (double)rounds - par,
		       best[i], worst[i]);
	free(s.bits);
	free(bg.bits);
	printf("rules-test --cpu: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}

int main(int argc, char **argv)
{
	uint32_t len;
	uint8_t *p;

	if (argc >= 3 && strcmp(argv[1], "--cpu") == 0)
		return cpu_course(argv[2], argc > 3 ? atoi(argv[3]) : 8, argc > 4 ? atoi(argv[4]) : 0);
	if (argc == 2 && strcmp(argv[1], "--dana-card") == 0) {
		dana_card_tests();
		printf("dana-card: %d checks, %d failed\n", checks, fails);
		return fails != 0;
	}
	if (argc < 4) { fprintf(stderr, "usage: rulestest COURSE.pdb ARTDIR OUTDIR, or rulestest --cpu COURSE.pdb [ROUNDS [TEE]]\n"); return 2; }
	artdir = argv[2];
	outdir = argv[3];
	p = load_art("Pal8-1000.bin", &len);
	CHECK(p && len == 768);
	if (p) { memcpy(pal, p, 768); free(p); }

	order_tests();
	total_tests();
	save_tests();
	card_tests();
	dana_card_tests();
	front_tests();
	book_edge_test();
	round_tests(argv[1]);
	sound_tests();
	printf("next scene drawn ahead: %d of %d matched, %d scenes not drawn ahead, %d known in flight\n", ahead_ok, ahead_n, ahead_none, ahead_fly);
	CHECK(ahead_n > 0 && ahead_ok == ahead_n && ahead_fly > 0);
	printf("rules-test: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
