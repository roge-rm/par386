/* Rules test. Run by `make rules-test`:
 *   rulestest COURSE.pdb ARTDIR OUTDIR
 * Checks the turn order, the honour, scores and the scorecard totals, the
 * saved round's record and bad records, then plays a 2 player round of the
 * test course through play.c, saving and restoring it on the way. Draws the
 * scorecard, the strips and the clubhouse with Continue round and the
 * players screen at 160x160 2-bit and 320x320 8-bit to OUTDIR as PPMs.
 * Last, the sounds hole 1 of the test course makes and the menu clicks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "play.h"
#include "sound.h"
#include "front.h"

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
	uint16_t len;
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
	CHECK(len == 2 + 32 + 4 + 9 + 18 + 4 * (9 + 11 + 18));
	CHECK(buf[0] == 0 && buf[1] == RULES_SAVE_VERSION);
	memset(&q, 0x55, sizeof q);
	CHECK(rules_unpack(&q, buf, len) == 0);
	CHECK(memcmp(&q, &r, sizeof r) == 0);

	/* a short round for 2 */
	rules_init(&r, "Par386 Test", 2, 3, pars, 0xDEADBEEFUL);
	len = rules_pack(&r, buf);
	CHECK(len == 2 + 32 + 4 + 9 + 3 + 2 * (9 + 11 + 3));
	CHECK(rules_unpack(&q, buf, len) == 0 && memcmp(&q, &r, sizeof r) == 0);

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
	BAD(47 + 3 + 9 + 8, LIE_COUNT);           /* a lie past the last */
	BAD(47 + 3 + 9 + 8 + 2, 2);               /* holed isn't 0 or 1 */
	BAD(47 + 3 + 8, 'x');                     /* a name with no end */
	BAD(47 + 3 + 9, 0x7F);                    /* a ball far off the hole */
	BAD(47 + 3 + 9 + 8 + 2, 1);               /* the turn is a holed player's */
	BAD(47 + 3 + 9 + 11 + 1, 4);              /* strokes on a hole not reached */
	memcpy(bad, buf, len);                    /* player 1 holed, player 2 to play: fine */
	bad[42] = 1; bad[69] = 1;
	CHECK(rules_unpack(&q, bad, len) == 0);
	bad[92] = 1;                              /* everyone holed but the hole not over */
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

/* Frames from t until the state changes or the view wants drawing again, as the platform's loop. */
static int run(Play *p, Surface *s, Surface *bg, uint32_t *t, uint8_t until_not)
{
	int f = 0, k;
	for (k = 0; k < 4000 && p->state == until_not; k++) {
		*t += 20;
		f = play_frame(p, s, bg, *t);
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

	resume_tests(&c);

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
			tap_at(rt + 2000, 140, 156);
			snap(rt + 3500, "a-card");
			card_layout(&d, &r, CARD_VIEW, 0);
			tap_at(rt + 4000, d.btn[0].x + d.btn[0].w / 2, d.btn[0].y + d.btn[0].h / 2);
			rt += 5500;
			CHECK(play_key(&p, KEY_OTHER, t) == PL_MAP);
			play_map(&s, &p);
			play_status(&s, &p, NULL);
			save(&s, "map-card-button");
			CHECK(play_tap(&p, 140, 156, t) == PL_CARD && p.card == CARD_VIEW);
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
					rules_init(&r, "x", pl, n, pars, 1);
					card_layout(&d, &r, mode, page);
					CHECK(d.box.x >= 0 && d.box.y >= 0 && d.box.x + d.box.w <= 160 && d.box.y + d.box.h <= 160);
					CHECK(d.count == (n > 9 ? 2 : 1));
					CHECK(card_hit(&r, mode, page, d.btn[0].x + 2, d.btn[0].y + 2) == 0);
					CHECK(card_hit(&r, mode, page, d.box.x + 4, d.box.y + 20) == -1);
					if (d.count > 1) {
						CHECK(d.btn[1].x + d.btn[1].w <= d.box.x + d.box.w);
						CHECK(card_hit(&r, mode, page, d.btn[1].x + 2, d.btn[1].y + 2) == 1);
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

/* ---- the clubhouse ---- */

static void draw_front(Surface *s, const Front *f)
{
	char name[64];
	uint16_t id = front_picture(f), k;

	gfx_fill(s, 0, 0, s->w, s->h, UIC_BLACK);
	for (k = 0; ; k++) {
		uint32_t len;
		uint8_t *d;
		snprintf(name, sizeof name, "Pic%d-%d.bin", s->depth, id + k);
		if (!(d = load_art(name, &len))) break;
		CHECK(gfx_pic_band(s, d, len) == 0);
		free(d);
	}
	CHECK(k > 0);
	front_draw(f, s);
}

static void front_tests(void)
{
	Front f;
	Surface s;
	uint8_t scale;

	/* no save: as before, Play a round goes to the players */
	front_init(&f);
	front_clubhouse(&f, 0);
	CHECK(f.menu.count == 5 && front_action(&f, 0) == MAIN_PLAY);
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_PLAYERS && f.menu.sel == 0);
	CHECK(front_key(&f, KEY_DOWN) == (FR_REDRAW | FR_LINES));
	CHECK(front_key(&f, KEY_SELECT) == FR_PLAY && f.players == 2);
	/* Back goes back to the clubhouse */
	front_clubhouse(&f, 0);
	front_key(&f, KEY_SELECT);
	f.menu.sel = 4;
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_MENU && f.menu.count == 5);
	/* taps pick a number */
	front_key(&f, KEY_SELECT);
	CHECK(front_tap(&f, 60, f.menu.y + 3 * f.menu.lineH + 2) & FR_PLAY);
	CHECK(f.players == 4);

	/* with a save: Continue round first, the rest a line down */
	front_clubhouse(&f, 1);
	CHECK(f.menu.count == 6 && front_action(&f, 0) == MAIN_CONTINUE && front_action(&f, 1) == MAIN_PLAY &&
	      front_action(&f, 5) == MAIN_QUIT);
	CHECK(f.menu.y + 5 * f.menu.lineH + UI_CHAR < 150);
	CHECK(front_key(&f, KEY_SELECT) == FR_CONTINUE);
	f.menu.sel = 5;
	CHECK(front_key(&f, KEY_SELECT) == FR_QUIT);
	/* a new round asks first: Cancel keeps the save, OK goes on to the players */
	f.menu.sel = 1;
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.dialog == DLG_NEW_ROUND && f.dlg.count == 2);
	CHECK(front_key(&f, KEY_RIGHT) == FR_REDRAW && f.dlg.sel == 1);
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && !f.dialog && f.screen == SCR_MENU);
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.dialog == DLG_NEW_ROUND);
	CHECK(front_tap(&f, f.dlg.btn[0].x + 4, f.dlg.btn[0].y + 4) == FR_REDRAW && f.screen == SCR_PLAYERS);
	/* Practice still opens its message */
	front_clubhouse(&f, 1);
	f.menu.sel = 2;
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.dialog == DLG_MESSAGE);

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
		save(&s, "players");
		free(s.bits);
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
		"splash", "cup", "oob", "holed", "birdie", "bogey", "menu-move", "menu-select"
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
		case SND_LAND_FAIRWAY: case SND_LAND_ROUGH: case SND_LAND_SAND: case SND_SPLASH: lands++; break;
		case SND_CUP: cups++; break;
		case SND_HOLED: case SND_BIRDIE: case SND_BOGEY: tunes++; break;
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
		while (snd_take() != SND_NONE) {}
	}

	/* menus: a move, a pick, the settings screen's sound line going round */
	snd_log_n = 0;
	front_init(&f);
	front_clubhouse(&f, 0);
	CHECK(front_key(&f, KEY_DOWN) & FR_LINES);
	CHECK(snd_log_n == 1 && snd_log[0] == SND_MENU_MOVE);
	f.menu.sel = MAIN_SETTINGS;
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_SETTINGS && f.menu.count == 2);
	CHECK(snd_log_n == 2 && snd_log[1] == SND_MENU_SELECT);
	CHECK(strcmp(f.settings_list, "Sound: On|Back") == 0);
	draw_front(&s, &f);
	save(&s, "settings");
	CHECK(front_key(&f, KEY_SELECT) == (FR_REDRAW | FR_SOUND) && f.sound == SOUND_FX && f.menu.sel == 0);
	CHECK(strcmp(f.settings_list, "Sound: Effects|Back") == 0);
	draw_front(&s, &f);
	save(&s, "settings-fx");
	CHECK(front_key(&f, KEY_SELECT) == (FR_REDRAW | FR_SOUND) && f.sound == SOUND_OFF);
	CHECK(front_key(&f, KEY_SELECT) == (FR_REDRAW | FR_SOUND) && f.sound == SOUND_ON);
	CHECK(front_key(&f, KEY_DOWN) & FR_LINES);
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_MENU && f.menu.sel == MAIN_SETTINGS);
	front_key(&f, KEY_SELECT);
	CHECK(f.screen == SCR_SETTINGS && f.menu.sel == 0);
	CHECK(front_key(&f, KEY_BACK) == FR_REDRAW && f.screen == SCR_MENU);
	free(s.bits);
	free(bg.bits);
	while (snd_take() != SND_NONE) {}
}

int main(int argc, char **argv)
{
	uint32_t len;
	uint8_t *p;

	if (argc < 4) { fprintf(stderr, "usage: rulestest COURSE.pdb ARTDIR OUTDIR\n"); return 2; }
	artdir = argv[2];
	outdir = argv[3];
	p = load_art("Pal8-1000.bin", &len);
	CHECK(p && len == 768);
	if (p) { memcpy(pal, p, 768); free(p); }

	order_tests();
	total_tests();
	save_tests();
	card_tests();
	front_tests();
	round_tests(argv[1]);
	sound_tests();
	printf("rules-test: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
