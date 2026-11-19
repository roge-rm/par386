#include "play.h"
#include "sound.h"
#include "cpu.h"

/* What play_frame draws again. */
#define R_STRIP  1
#define R_OVER   2      /* the aim marker or the ball moved */
#define R_METER  4
#define R_ALL    8      /* the scene under the overlays is new */

#define STRIP_Y   (s->lh - RENDER_STATUS)   /* the strip's top in logical units */
#define FLY_MS    42    /* a path point in the air: 1.5 times real time */
#define ROLL_MS   62    /* a point on the ground: twice real time */
#define RESULT_MS 1500
#define AIM_STEP  182   /* a degree */
#define AIM_EDGE  56    /* the marker this far off the centre draws the scene again */
#define ROW_A     (STRIP_Y + 2)     /* the panel's two rows of boxes */
#define ROW_B     (STRIP_Y + 14)
#define ROW_H     11
#define BOX_W(n)  ((n) * UI_CHAR + 3)       /* a box for n characters */
#define CLUB_X    2
#define CLUB_W    BOX_W(6)
#define SHAPE_X   54                /* the shape button */
#define SHAPE_W   BOX_W(5)
#define PIN_X     98
#define PIN_W     BOX_W(4)
#define ROSE_W    24                /* the wind rose, both rows high, at the right */
#define ROSE_X    (s->lw - 2 - ROSE_W)
#define LIE_X     2
#define LIE_W     BOX_W(8)
#define WHO_X     70
#define MAP_W     BOX_W(3)          /* the Map button, left of the rose */
#define MAP_X     (ROSE_X - 1 - MAP_W)
#define CARD_W    BOX_W(4)          /* the Card button on the map's panel */
#define CARD_X    (s->lw - 2 - CARD_W)
#define S16       (MTR_SNAP * 16)
#define W16       (MTR_W * 16)

static void SEC_UI launch(Play *p);
static void SEC_UI stopped(const Play *p);
static void SEC_UI result(Play *p);

static vec2 SEC_UI pin(const Play *p)
{
	return p->rv.hole->pins[p->rv.pin & 3];
}

static uint16_t SEC_UI pin_yards(const Play *p)
{
	return (uint16_t)((vec2_len(vec2_sub(pin(p), p->rv.ball)) + FIX_HALF) >> 16);
}

static uint16_t SEC_UI pin_feet(const Play *p)
{
	return (uint16_t)((vec2_len(vec2_sub(pin(p), p->rv.ball)) * 3 + FIX_HALF) >> 16);
}

/* Yards to the aim marker: the club's carry with the shape, or for the putter the pin. */
static uint16_t SEC_UI mark_yards(const Play *p)
{
	uint16_t d;
	if (p->club != CLUB_PT) return ball_shape_carry(p->club, p->lie, p->shape);
	d = pin_yards(p);
	return d > BALL_PUTT_MAX ? BALL_PUTT_MAX : d < 1 ? 1 : d;
}

/* Sets the club; the putter goes with the green view and a meter scaled
 * to half as far again as the cup, 3 to BALL_PUTT_MAX yards. */
static void SEC_UI set_club(Play *p, uint8_t club)
{
	fixed r;
	p->club = club;
	if (!ball_shape_ok(club, p->shape)) p->shape = SHAPE_NORMAL;
	p->rv.green = club == CLUB_PT;
	r = vec2_len(vec2_sub(pin(p), p->rv.ball));
	r = ((r + (r >> 1) + FIX_ONE - 1) >> 16) << 16;
	if (r < (3L << 16)) r = 3L << 16;
	if (r > ((fixed)BALL_PUTT_MAX << 16)) r = (fixed)BALL_PUTT_MAX << 16;
	p->range = r;
}

/* Gets ready for the next shot from rv.ball: aim, club and state. On the
 * green and the fringe it's the putter. */
static void SEC_UI setup(Play *p)
{
	const Hole *h = p->rv.hole;
	vec2 to = pin(p);

	if (p->rv.ball.x == h->tees[0].pos.x && p->rv.ball.y == h->tees[0].pos.y) to = h->aim;
	p->rv.aim = vec2_angle(vec2_sub(to, p->rv.ball));
	p->aim = p->rv.aim;
	p->shape = SHAPE_NORMAL;
	set_club(p, p->lie == LIE_GREEN || p->lie == LIE_FRINGE ? CLUB_PT : ball_pick(pin_yards(p), p->lie));
	p->state = PS_AIM;
	p->cam_ok = 0;
	p->redraw = R_ALL;
	p->shown = -2;
	p->mishit = 0;
	snd_ambient(p->lie == LIE_TEE);
}

void play_init(Play *p, const Hole *h, const SurfaceGrid *g, uint32_t seed)
{
	uint8_t *b = (uint8_t *)p;
	uint16_t i;

	for (i = 0; i < sizeof(Play); i++) b[i] = 0;
	p->rv.hole = h;
	p->rv.grid = g;
	p->rv.pin = 0;
	p->rv.ball = h->tees[0].pos;
	p->lie = LIE_TEE;
	p->stroke = 1;
	p->view = PLAY_SCENE;
	p->geo.lw = p->geo.lh = 160;
	p->geo.scale = 1;
	ball_wind(seed, h->number, h->wind, &p->wind_mph, &p->wind_dir);
	setup(p);
}

static uint8_t SEC_UI practice(const Play *p)
{
	return p->round && MODE_PRACTICE(p->round->mode);
}

/* The practice green's next ball: 3 to 14 yd from the pin on the green,
 * from the round's seed and the balls played, so a saved round puts it
 * back in the same place. */
static void SEC_UI green_spot(Play *p)
{
	uint32_t rs = rules_shot_seed(p->round, 0);
	vec2 to = pin(p), at;
	uint8_t k;

	for (k = 0; k < 12; k++) {
		fixed d;
		angle a;
		rs = rs * 1664525UL + 1013904223UL;
		d = (fixed)(3 + (rs >> 24) % 12) << 16;
		a = (angle)(rs >> 8);
		at = vec2_make(to.x + fix_mulq14(d, icos(a)), to.y + fix_mulq14(d, isin(a)));
		if (surface_at(p->rv.grid, at.x, at.y) == SURF_GREEN) break;
	}
	if (k == 12) at = vec2_make(to.x - (2L << 16), to.y - (7L << 16));
	p->rv.ball = at;
	p->lie = LIE_GREEN;
}

/* A computer golfer's shot, planned from where their ball lies. With quick
 * it's played at once and the map shows the result; else the scene is
 * drawn and they swing once it has shown a moment. */
static void SEC_UI cpu_turn(Play *p)
{
	CpuView v;
	CpuShot c;

	v.hole = p->rv.hole;
	v.grid = p->rv.grid;
	v.ball = p->rv.ball;
	v.pin = pin(p);
	if (p->lie == LIE_TEE) v.to = p->rv.hole->aim;
	else v.to = v.pin;
	v.lie = p->lie;
	v.wind_mph = p->wind_mph;
	v.wind_dir = p->wind_dir;
	v.range = p->range;
	cpu_shot(&v, p->cpu, rules_shot_seed(p->round, p->cur), &c);
	set_club(p, c.club);
	p->shape = c.shape;
	p->aim = p->rv.aim = c.aim;
	p->power = c.power;
	p->snap = c.snap;
	p->mishit = 0;
	p->wait = 1;
	if (p->quick) {
		launch(p);
		stopped(p);
		result(p);
		p->state = PS_RESULT;
		p->view = PLAY_MAP;
	}
}

/* Player i's turn: their ball, lie and stroke, and the view behind it. */
static void SEC_UI take(Play *p, uint8_t i)
{
	Round *r = p->round;
	const RoundPlayer *rp = &r->pl[i];

	p->cur = i;
	r->turn = i;
	if (rp->taken) {
		p->rv.ball = rp->ball;
		p->lie = rp->lie;
	} else if (r->mode == MODE_GREEN) {
		green_spot(p);
	} else {
		p->rv.ball = p->rv.hole->tees[0].pos;
		p->lie = LIE_TEE;
	}
	p->stroke = (uint8_t)(rp->taken + 1);
	p->have_shot = 0;
	p->applied = 0;
	p->view = PLAY_SCENE;
	p->cpu = rp->cpu;
	setup(p);
	if (p->cpu) cpu_turn(p);
}

/* What the platform does for a turn take gave: the map for a quick
 * computer golfer's result, else the scene drawn with the turn panel. */
static int SEC_UI turn(const Play *p)
{
	return p->view == PLAY_MAP ? PL_MAP | PL_ANIM : PL_RENDER | PL_TURN;
}

void play_start(Play *p, const Hole *h, const SurfaceGrid *g, Round *r)
{
	int n;
	uint8_t quick = p->quick;

	play_init(p, h, g, r->seed);
	p->quick = quick;
	p->round = r;
	/* r->turn may still be the player who just hit; the rules pick who is next */
	n = rules_next(r, pin(p));
	take(p, (uint8_t)(n < 0 ? 0 : n));
}

/* Opens the scorecard on the page with the hole being played, or the one
 * just finished. */
static int SEC_UI open_card(Play *p, uint8_t mode)
{
	const Round *r = p->round;
	uint8_t h = (uint8_t)(mode == CARD_HOLE ? r->hole - 1 : r->hole);
	p->card_from = p->view;
	p->view = PLAY_CARD;
	p->card = mode;
	p->page = (uint8_t)(h / 9);
	p->csel = 0;
	p->board = 0;
	return PL_CARD;
}

/* The meter's marker at now, 1/16 units from the bar's left. */
static int16_t SEC_UI meter_pos(const Play *p, uint32_t now)
{
	/* the times are clamped (past the end of either run) so the product stays small after a long pause */
	uint32_t e;
	if (p->state == PS_UP) {
		e = now - p->t0;
		if (e > 2 * MTR_UP_MS) e = 2 * MTR_UP_MS;
		return (int16_t)(S16 + (int32_t)(W16 - S16) * (int32_t)e / MTR_UP_MS);
	}
	e = now - p->t1;
	if (e > 2 * MTR_DOWN_MS) e = 2 * MTR_DOWN_MS;
	return (int16_t)(p->turn - (int32_t)(W16 - S16) * (int32_t)e / MTR_DOWN_MS);
}

static void SEC_UI launch(Play *p)
{
	ShotIn in;

	in.hole = p->rv.hole;
	in.grid = p->rv.grid;
	in.pos = p->rv.ball;
	in.pin = pin(p);
	in.wind = ball_wind_vec(p->wind_mph, p->wind_dir);
	in.aim = p->aim;
	in.power = p->power;
	in.snap = p->snap;
	in.club = p->club;
	in.range = p->range;
	in.lie = p->lie;
	in.mishit = p->mishit;
	in.shape = p->shape;
	snd_play(p->club == CLUB_PT ? SND_HIT_PUTT : p->club <= CLUB_5W ? SND_HIT_WOOD : SND_HIT_IRON);
	ball_shot(&in, &p->shot);
	p->have_shot = 1;
	p->state = PS_FLY;
	p->shown = -2;
	p->redraw |= R_OVER;
}

/* The sound of the ball first coming down, once the flight gets to point
 * i: the last point if the shot ends before landing (in the water). */
static void SEC_UI landed(const Play *p, int16_t i)
{
	const Shot *sh = &p->shot;
	int16_t l = sh->fly_n < sh->n - 1 ? sh->fly_n : sh->n - 1;
	uint8_t id;

	if (p->club == CLUB_PT || !sh->fly_n || p->shown >= l || i < l) return;
	switch (sh->land_surf) {
	case SURF_WATER: id = SND_SPLASH; break;
	case SURF_BUNKER: id = SND_LAND_SAND; break;
	case SURF_ROUGH: case SURF_DEEP_ROUGH: id = SND_LAND_ROUGH; break;
	case SURF_OOB: return;
	default: id = SND_LAND_FAIRWAY; break;
	}
	snd_play(id);
}

/* The sound of how the shot ended, as the ball stops. */
static void SEC_UI stopped(const Play *p)
{
	const Shot *sh = &p->shot;
	if (sh->result == SHOT_HOLED) snd_play(SND_CUP);
	else if (sh->result == SHOT_OOB) snd_play(SND_OOB);
	else if (sh->result == SHOT_WATER && (p->club == CLUB_PT || sh->land_surf != SURF_WATER)) snd_play(SND_SPLASH);
}

/* After a player's hole is done: the next player, or the scorecard once
 * everyone has holed out, with the round moved on to the next hole. */
static int SEC_UI after_done(Play *p)
{
	Round *r = p->round;
	int n;

	if (!r) return PL_BACK;
	n = rules_next(r, pin(p));
	if (n >= 0) {
		take(p, (uint8_t)n);
		return turn(p);
	}
	rules_advance(r);
	n = open_card(p, r->over ? CARD_FINAL : CARD_HOLE);
	/* a tournament shows the leaderboard first */
	p->board = r->mode == MODE_TOURN;
	return n;
}

/* The scorecard's button i. */
static int SEC_UI card_button(Play *p, uint8_t i)
{
	if (p->board) {
		/* the leaderboard: its first button does what the card's does, the second shows the card */
		if (i == 1) {
			p->board = 0;
			p->csel = 0;
			return PL_CARD;
		}
	} else if (i == card_board_button(p->round, p->card)) {
		/* the card's Board button */
		p->board = 1;
		p->csel = 0;
		return PL_CARD;
	} else if (i == 1) {
		p->page = (uint8_t)((p->page + 1) % card_pages(p->round));
		p->csel = 1;
		return PL_CARD;
	}
	if (p->card == CARD_HOLE) return PL_NEXT;
	if (p->card == CARD_FINAL) return PL_BACK;
	if (p->card_from == PLAY_MAP) {
		p->view = PLAY_MAP;
		return PL_MAP;
	}
	p->view = PLAY_SCENE;
	p->redraw = R_ALL;
	return PL_SCENE;
}

static int SEC_UI card_key(Play *p, uint8_t key)
{
	uint8_t pages = card_pages(p->round), n = p->board ? board_buttons(p->round, p->card) : card_buttons(p->round, p->card);
	switch (key) {
	case KEY_LEFT:
		if (n < 2) return 0;
		p->csel = (uint8_t)((p->csel + n - 1) % n);
		return PL_CARD;
	case KEY_RIGHT:
		if (n < 2) return 0;
		p->csel = (uint8_t)((p->csel + 1) % n);
		return PL_CARD;
	case KEY_UP:
		if (pages < 2 || p->board) return 0;
		p->page = (uint8_t)((p->page + pages - 1) % pages);
		return PL_CARD;
	case KEY_DOWN:
		if (pages < 2 || p->board) return 0;
		p->page = (uint8_t)((p->page + 1) % pages);
		return PL_CARD;
	case KEY_SELECT:
		return card_button(p, p->csel);
	case KEY_CARD: case KEY_OTHER:
		/* the key that opened it closes it */
		return p->card == CARD_VIEW ? card_button(p, 0) : 0;
	}
	return 0;
}

/* A click of the meter: start, set the power, set the snap. */
static int SEC_UI click(Play *p, uint32_t now)
{
	int16_t pos;

	switch (p->state) {
	case PS_AIM:
		snd_ambient(0);
		snd_play(SND_SWING);
		p->state = PS_UP;
		p->t0 = now;
		p->mpos = S16;
		p->power = FIX_ONE;
		p->snap = 0;
		p->mishit = 0;
		p->redraw |= R_STRIP;
		return PL_ANIM;
	case PS_UP:
		pos = meter_pos(p, now);
		if (pos > W16) pos = W16;
		p->turn = pos;
		p->mpos = pos;
		p->t1 = now;
		p->power = ((fixed)(pos - S16) << 16) / (W16 - S16);
		if (p->power < 0) p->power = 0;
		p->redraw |= R_METER;
		if (p->club == CLUB_PT) {
			/* a putt has no snap: it goes on the power click */
			launch(p);
			return PL_ANIM;
		}
		snd_play(SND_CLICK);
		p->state = PS_DOWN;
		return PL_ANIM;
	case PS_DOWN: {
		/* when the marker crossed (or will cross) the snap line, and how far off the click was */
		uint32_t ts = p->t1 + (uint32_t)((int32_t)(p->turn - S16) * MTR_DOWN_MS / (W16 - S16));
		int32_t err = (int32_t)(now - ts), a = err < 0 ? -err : err;
		fixed e;
		a = a > 10 ? a - 10 : 0;
		e = a >= MTR_SNAP_MS ? FIX_ONE : (fixed)((a << 16) / MTR_SNAP_MS);
		p->snap = err > 0 ? -e : e;
		p->mpos = meter_pos(p, now);
		p->redraw |= R_METER;
		launch(p);
		return PL_ANIM;
	}
	case PS_RESULT:
		p->t0 = now - RESULT_MS;
		return PL_ANIM;
	case PS_DONE:
		return after_done(p);
	}
	return 0;
}

/* A key or tap on a computer golfer's turn hurries it on: their swing,
 * result or score goes on now. */
static int SEC_UI hurry(Play *p, uint32_t now)
{
	if (p->state == PS_FLY) return 0;
	p->wait = 0;
	p->tw = now - 10000L;
	p->t0 = now - RESULT_MS;
	return PL_ANIM;
}

int play_key(Play *p, uint8_t key, uint32_t now)
{
	if (key == KEY_BACK) return PL_BACK;
	if (p->view == PLAY_CARD) return card_key(p, key);
	if (p->cpu) return hurry(p, now);
	if (key == KEY_CARD && p->round && !practice(p) && (p->state == PS_AIM || p->view == PLAY_MAP)) return open_card(p, CARD_VIEW);
	if (p->view == PLAY_MAP) {
		p->view = PLAY_SCENE;
		p->redraw = R_ALL;
		return PL_SCENE;
	}
	if (p->state == PS_DONE) return after_done(p);
	if (key == KEY_SELECT) return click(p, now);
	if (p->state != PS_AIM) return 0;
	switch (key) {
	case KEY_OTHER: case KEY_MAP:
		p->view = PLAY_MAP;
		return PL_MAP;
	case KEY_SHAPE:
		p->shape = ball_shape_next(p->club, p->shape);
		p->redraw |= R_STRIP | R_OVER;
		break;
	case KEY_LEFT:
		p->aim = (angle)(p->aim + AIM_STEP);
		p->redraw |= R_OVER;
		break;
	case KEY_RIGHT:
		p->aim = (angle)(p->aim - AIM_STEP);
		p->redraw |= R_OVER;
		break;
	case KEY_UP:
	case KEY_DOWN: {
		/* on the green it's the putter only; to or from the putter changes the view */
		uint8_t c = p->club, g = p->rv.green;
		if (p->lie == LIE_GREEN) break;
		if (key == KEY_UP && c > CLUB_DR) c--;
		if (key == KEY_DOWN && c < CLUB_PT) c++;
		set_club(p, c);
		p->redraw |= R_OVER | R_STRIP;
		if (p->rv.green != g) {
			p->rv.aim = p->aim;
			p->cam_ok = 0;
			p->redraw = R_ALL;
			return PL_RENDER;
		}
		break;
	}
	}
	return 0;
}

static void SEC_UI camera(Play *p, const Surface *s)
{
	p->geo = *s;
	if (!p->cam_ok) render_camera(s, &p->rv, &p->cam);
	p->cam_ok = 1;
}

/* Where the aim marker shows for an aim, in pixels. */
static int SEC_UI mark_at(const Play *p, angle a, int16_t *x, int16_t *y)
{
	fixed d = (fixed)mark_yards(p) << 16, depth;
	vec2 m = vec2_make(p->rv.ball.x + fix_mulq14(d, icos(a)), p->rv.ball.y + fix_mulq14(d, isin(a)));
	return render_point(&p->cam, m.x, m.y, hole_height(p->rv.hole, m.x, m.y), x, y, &depth);
}

/* Turns the aim so the marker shows in pixel column tx. */
static void SEC_UI aim_at(Play *p, int16_t tx)
{
	int32_t d = mark_yards(p);
	angle a = (angle)(p->rv.aim - iatan2((int32_t)(tx - p->cam.cx), p->cam.f));
	int16_t i, mx, my;

	/* the marker is d yards past the ball and the camera 14 behind, so it moves less than the angle */
	for (i = 0; i < 4; i++) {
		int32_t e;
		if (!mark_at(p, a, &mx, &my)) break;
		e = tx - mx;
		if (e > -1 && e < 1) break;
		a = (angle)(a - iatan2(e * (d + p->cam.back) / d, p->cam.f));
	}
	p->aim = a;
	p->redraw |= R_OVER;
}

/* Whether (x, y) is on a box at bx, by, w wide, with a little slack for fingers. */
static int SEC_UI on_box(int16_t x, int16_t y, int16_t bx, int16_t by, int16_t w)
{
	return x >= bx - 2 && x < bx + w + 2 && y >= by - 2 && y < by + ROW_H + 2;
}

int play_tap(Play *p, int16_t x, int16_t y, uint32_t now)
{
	const Surface *s = &p->geo;

	if (p->view == PLAY_CARD) {
		int16_t cx = x - (s->lw - 160) / 2, cy = y - (s->lh - 160) / 2;
		int i = p->board ? board_hit(p->round, p->card, cx, cy) : card_hit(p->round, p->card, p->page, cx, cy);
		return i < 0 ? 0 : card_button(p, (uint8_t)i);
	}
	if (p->cpu) return hurry(p, now);
	if (p->view == PLAY_MAP) {
		if (p->round && !practice(p) && on_box(x, y, CARD_X, ROW_B, CARD_W)) return open_card(p, CARD_VIEW);
		return play_key(p, KEY_SELECT, now);
	}
	if (p->state == PS_DONE) return after_done(p);
	if (p->state == PS_UP || p->state == PS_DOWN || p->state == PS_RESULT) return click(p, now);
	if (y >= STRIP_Y) {
		/* the buttons, and the rest of the panel swings */
		if (p->state != PS_AIM) return 0;
		if (on_box(x, y, MAP_X, ROW_B, MAP_W)) return play_key(p, KEY_OTHER, now);
		if (p->club != CLUB_PT && on_box(x, y, SHAPE_X, ROW_A, SHAPE_W)) return play_key(p, KEY_SHAPE, now);
		return click(p, now);
	}
	if (p->state == PS_AIM && p->cam_ok) aim_at(p, (int16_t)(x * s->scale));
	return 0;
}

/* Adds a pixel rectangle to the dirty one. */
static void SEC_UI dirty(Play *p, int16_t x, int16_t y, int16_t w, int16_t h)
{
	URect *d = &p->dirty;
	if (w <= 0 || h <= 0) return;
	if (d->w <= 0) {
		d->x = x; d->y = y; d->w = w; d->h = h;
		return;
	}
	if (x < d->x) { d->w += d->x - x; d->x = x; }
	if (y < d->y) { d->h += d->y - y; d->y = y; }
	if (x + w > d->x + d->w) d->w = x + w - d->x;
	if (y + h > d->y + d->h) d->h = y + h - d->y;
}

/* A fill over the scene, kept to the view. */
static void SEC_UI over(Surface *s, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c)
{
	int16_t vh = (int16_t)(STRIP_Y * s->scale);
	if (y + h > vh) h = vh - y;
	if (h > 0) gfx_fill(s, x, y, w, h, c);
}

/* Remembers pixels drawn over the scene, to take off again from the kept copy. */
static void SEC_UI patch(Play *p, int16_t x, int16_t y, int16_t w, int16_t h)
{
	const Surface *s = &p->geo;
	int16_t vh = (int16_t)(STRIP_Y * s->scale);
	if (y + h > vh) h = vh - y;
	if (h <= 0) return;
	if (p->npatch < PLAY_PATCHES) {
		URect *r = &p->patch[p->npatch++];
		r->x = x; r->y = y; r->w = w; r->h = h;
	}
	dirty(p, x, y, w, h);
}

/* The aim marker: a white cross with a black edge, so it shows on dark
 * trees and light grass alike. */
static void SEC_UI draw_mark(Play *p, Surface *s, int16_t x, int16_t y)
{
	int16_t u = s->scale;
	over(s, x - 4 * u, y - u, 9 * u, 3 * u, UIC_BLACK);
	over(s, x - u, y - 4 * u, 3 * u, 9 * u, UIC_BLACK);
	over(s, x - 3 * u, y, 7 * u, u, UIC_WHITE);
	over(s, x, y - 3 * u, u, 7 * u, UIC_WHITE);
	patch(p, x - 4 * u, y - 4 * u, 9 * u, 9 * u);
}

/* The green view's aim: dots from the ball out to the cup's distance, white
 * with a black edge. */
static void SEC_UI aim_dots(Play *p, Surface *s)
{
	fixed d = (fixed)mark_yards(p) << 16, depth;
	int16_t k, x, y, u = s->scale;
	for (k = 1; k <= 8; k++) {
		fixed t = d / 8 * k;
		vec2 m = vec2_make(p->rv.ball.x + fix_mulq14(t, icos(p->aim)), p->rv.ball.y + fix_mulq14(t, isin(p->aim)));
		if (!render_point(&p->cam, m.x, m.y, hole_height(p->rv.hole, m.x, m.y), &x, &y, &depth)) continue;
		over(s, x - u, y - u, 3 * u, 3 * u, UIC_BLACK);
		over(s, x, y, u, u, UIC_WHITE);
		patch(p, x - u, y - u, 3 * u, 3 * u);
	}
}

static void SEC_UI path_pt(const Play *p, int16_t k, fixed *x, fixed *y, fixed *z)
{
	const BallPt *b = &p->shot.path[k];
	*x = (fixed)b->x * 4096;
	*y = (fixed)b->y * 4096;
	*z = (fixed)b->z * 4096;
}

/* The ball at (x, y, z), bigger near the camera and always big in the
 * green view. White with a dark ring so it shows on the sky and on grass. */
static void SEC_UI ball_at(Play *p, Surface *s, fixed x, fixed y, fixed z)
{
	int16_t sx, sy, u = s->scale, n;
	fixed depth;

	if (!render_point(&p->cam, x, y, z, &sx, &sy, &depth)) return;
	n = depth < (50L << 16) || p->rv.green ? 2 * u : u;
	over(s, sx - 1, sy - 1, n + 2, n + 2, s->depth >= 8 ? UIC_BALL_DK : UIC_BLACK);
	over(s, sx, sy, n, n, UIC_WHITE);
	patch(p, sx - 1, sy - 1, n + 2, n + 2);
}

/* The ball at path point shown, with a trail of the points before it in
 * the air, smaller with distance. Once it's holed it's gone. */
static void SEC_UI draw_ball(Play *p, Surface *s)
{
	int16_t k = p->shown, j, sx, sy, u = s->scale;
	uint8_t grey = s->depth >= 8;
	fixed x, y, z, depth;

	for (j = k - 8; j < k; j += 2) {
		if (j < 0 || j >= p->shot.fly_n || p->club == CLUB_PT) continue;
		path_pt(p, j, &x, &y, &z);
		if (render_point(&p->cam, x, y, z, &sx, &sy, &depth)) {
			over(s, sx, sy, u, u, grey ? UIC_BALL_LT : UIC_BLACK);
			patch(p, sx, sy, u, u);
		}
	}
	if (p->shot.result == SHOT_HOLED && k == p->shot.n - 1) return;
	path_pt(p, k, &x, &y, &z);
	ball_at(p, s, x, y, z);
}

/* Appends s to t at *n. */
static void SEC_UI add(char *t, int16_t *n, const char *s)
{
	while (*s) t[(*n)++] = *s++;
	t[*n] = 0;
}

/* The panel: a raised face with two rows of boxes, ROW_H units each.
 * Readouts are sunken wells with light text, buttons raised brass, as in
 * the dialogs. On greys the face is white and the wells black. */
static void SEC_UI face(Surface *s)
{
	ui_fill(s, 0, STRIP_Y, s->lw, 1, UIC_BLACK);
	ui_fill(s, 1, STRIP_Y + 2, s->lw - 2, RENDER_STATUS - 3, UIC_PAPER);
	ui_bevel(s, 0, STRIP_Y + 1, s->lw, RENDER_STATUS - 1, UIC_WHITE, UIC_PAPER_DK);
}

static void SEC_UI well(Surface *s, int16_t x, int16_t y, int16_t w, int16_t h)
{
	ui_fill(s, x + 1, y + 1, w - 2, h - 2, UIC_TITLE);
	ui_bevel(s, x, y, w, h, UIC_INK, UIC_WHITE);
}

/* Text in a box w wide at row y, left, centred or right (align -1, 0, 1),
 * cut to what fits. n below 0 takes the whole string. */
static void SEC_UI box_text(Surface *s, int16_t x, int16_t y, int16_t w, const char *t, int16_t n, int8_t align, uint8_t c)
{
	int16_t room = (w - 3) / UI_CHAR;
	if (n < 0) for (n = 0; t[n]; n++) {}
	if (n > room) n = room;
	if (align > 0) x += w - 1 - n * UI_CHAR;
	else if (align == 0) x += (w + 1 - n * UI_CHAR) / 2;
	else x += 2;
	ui_text(s, x, y + 2, t, n, c, -1);
}

static void SEC_UI readout(Surface *s, int16_t x, int16_t y, int16_t w, const char *t, int8_t align, uint8_t c)
{
	well(s, x, y, w, ROW_H);
	box_text(s, x, y, w, t, -1, align, c);
}

static void SEC_UI button(Surface *s, int16_t x, int16_t y, int16_t w, const char *t)
{
	ui_fill(s, x, y, w, ROW_H, UIC_BRASS);
	ui_bevel(s, x, y, w, ROW_H, UIC_BRASS_LT, UIC_BRASS_DK);
	box_text(s, x, y, w, t, -1, 0, UIC_INK);
}

/* Wind arrows, 9 x 9, bit 8 the left column: up (with the shot) and
 * up-left; the other six are these turned by quarter turns. */
static const uint16_t arrows[2][9] = {
	{ 0x010, 0x038, 0x054, 0x092, 0x010, 0x010, 0x010, 0x010, 0x010 },
	{ 0x000, 0x0F0, 0x0C0, 0x0A0, 0x090, 0x008, 0x004, 0x002, 0x000 },
};

/* Whether arrow d (0 up, then on round to the left in eighths) has row r, column c. */
static int SEC_UI arrow_bit(uint8_t d, int16_t r, int16_t c)
{
	uint8_t k;
	for (k = d >> 1; k; k--) {
		int16_t t = r;
		r = c;
		c = 8 - t;
	}
	return (arrows[d & 1][r] >> (8 - c)) & 1;
}

/* The wind rose: a ring with the shot's way marked at the top, an arrow
 * the way the wind blows and its speed in mph below. Empty on the green. */
static void SEC_UI rose(const Play *p, Surface *s)
{
	int16_t x = ROSE_X, y = ROW_A, u = s->scale, r2 = 12 * u, i, j, n;
	int16_t cx = (int16_t)(2 * (x + 12) * u + u), cy = (int16_t)(2 * (y + 7) * u + u);
	int32_t lo = (int32_t)(r2 - u) * (r2 - u), hi = (int32_t)(r2 + u) * (r2 + u);
	char t[4];

	well(s, x, y, ROSE_W, 2 * ROW_H + 1);
	for (j = (cy - r2 - u) >> 1; j <= (cy + r2 + u) >> 1; j++) {
		int32_t e = 2 * j + 1 - cy;
		for (i = (cx - r2 - u) >> 1; i <= (cx + r2 + u) >> 1; i++) {
			int32_t d = 2 * i + 1 - cx;
			d = d * d + e * e;
			if (d >= lo && d < hi) gfx_fill(s, i, j, 1, 1, UIC_PAPER_DK);
		}
	}
	/* the shot's way */
	ui_fill(s, x + 12, y + 1, 1, 1, UIC_PAPER);
	if (p->club == CLUB_PT) return;
	if (p->wind_mph) {
		uint8_t d = (uint8_t)((((uint16_t)(p->wind_dir - p->aim) + 4096) >> 13) & 7);
		for (j = 0; j < 9; j++)
			for (i = 0; i < 9; i++)
				if (arrow_bit(d, j, i)) ui_fill(s, x + 8 + i, y + 3 + j, 1, 1, UIC_TEXT_HI);
	}
	n = play_num(t, p->wind_mph);
	box_text(s, x, y + 12, ROSE_W, t, n, 0, UIC_PAPER);
}

/* The shot or the player, in room characters: "2nd" (or "Shot 2" if
 * there's room) alone, or the player's name, "P2" if it's too long. */
static void SEC_UI who(const Play *p, char *t, int16_t room)
{
	int16_t n = 0;
	if (p->round && p->round->players > 1) {
		const char *nm = p->round->pl[p->cur].name;
		for (n = 0; nm[n]; n++) {}
		if (n <= room) {
			n = 0;
			add(t, &n, nm);
		} else {
			t[0] = 'P';
			t[1] = (char)('1' + p->cur);
			t[2] = 0;
		}
		return;
	}
	if (room >= 7) add(t, &n, "Shot ");
	n += play_num(t + n, p->stroke);
	if (room < 7) {
		uint8_t v = p->stroke;
		add(t, &n, v % 100 / 10 == 1 || v % 10 > 3 || v % 10 == 0 ? "th" : v % 10 == 1 ? "st" : v % 10 == 2 ? "nd" : "rd");
	}
}

/* Writes a score against par: "E", "+3", "-1". */
static void SEC_UI to_par(char *t, int16_t *n, int16_t v)
{
	if (!v) {
		add(t, n, "E");
		return;
	}
	add(t, n, v > 0 ? "+" : "-");
	*n += play_num(t + *n, (uint32_t)(v > 0 ? v : -v));
}

/* The panel's row B box: in a round, the mode's standing for the player
 * whose shot it is (the score to par in stroke play and tournaments,
 * holes up or down in match play and best ball, the skins riding on this
 * hole, the ball in practice), with "P2 " in front if there's room; for a
 * hole alone, who() in room characters. */
static void SEC_UI standing(const Play *p, char *t, int16_t room)
{
	const Round *r = p->round;
	char v[12];
	int16_t n = 0, k;

	if (!r) {
		who(p, t, room);
		return;
	}
	switch (r->mode) {
	case MODE_RANGE: case MODE_GREEN:
		add(v, &n, room >= 7 ? "Ball " : "#");
		n += play_num(v + n, r->balls < 998 ? (uint32_t)r->balls + 1 : 999);
		break;
	case MODE_MATCH: case MODE_BEST: {
		uint8_t left;
		int8_t up = rules_match(r, &left);
		if (rules_side(r, p->cur)) up = (int8_t)-up;
		if (!up) {
			add(v, &n, "AS");
		} else {
			n = play_num(v, (uint32_t)(up > 0 ? up : -up));
			add(v, &n, up > 0 ? " up" : " dn");
		}
		break;
	}
	case MODE_SKINS: {
		uint8_t won[RULES_PLAYERS], carry;
		rules_skins(r, won, &carry);
		add(v, &n, "Sk ");
		n += play_num(v + n, (uint32_t)carry + 1);
		break;
	}
	default:
		to_par(v, &n, rules_to_par(r, p->cur));
		break;
	}
	k = 0;
	t[0] = 0;
	if (r->players > 1 && room >= n + 3) {
		t[0] = 'P';
		t[1] = (char)('1' + p->cur);
		t[2] = ' ';
		t[3] = 0;
		k = 3;
	}
	add(t, &k, v);
}

static void SEC_UI draw_strip(Play *p, Surface *s)
{
	char t[24];
	int16_t n = 0;
	uint8_t swing = p->state == PS_UP || p->state == PS_DOWN;

	face(s);
	if (p->state == PS_RESULT || p->state == PS_DONE) {
		/* the result in one box, two lines */
		well(s, 2, ROW_A, s->lw - 4, 2 * ROW_H + 1);
		box_text(s, 2, ROW_A, s->lw - 4, p->msg1, -1, 0, UIC_TEXT_HI);
		box_text(s, 2, ROW_B, s->lw - 4, p->msg2, -1, 0, UIC_PAPER);
	} else {
		if (!swing) {
			if (p->club == CLUB_PT) {
				/* the putter and the cup in feet */
				readout(s, CLUB_X, ROW_A, CLUB_W, "Putter", -1, UIC_PAPER);
				add(t, &n, "Cup ");
				n += play_num(t + n, pin_feet(p));
				add(t, &n, " ft");
				readout(s, SHAPE_X, ROW_A, PIN_X + PIN_W - SHAPE_X, t, 1, UIC_TEXT_HI);
			} else {
				/* club and carry, the shape, yards to the pin */
				add(t, &n, ball_club_name(p->club));
				add(t, &n, " ");
				n += play_num(t + n, mark_yards(p));
				readout(s, CLUB_X, ROW_A, CLUB_W, t, -1, UIC_PAPER);
				if (p->state == PS_AIM) button(s, SHAPE_X, ROW_A, SHAPE_W, ball_shape_name(p->shape));
				else readout(s, SHAPE_X, ROW_A, SHAPE_W, p->shape ? ball_shape_name(p->shape) : "", 0, UIC_PAPER);
				n = play_num(t, pin_yards(p));
				add(t, &n, "y");
				readout(s, PIN_X, ROW_A, PIN_W, t, 1, UIC_TEXT_HI);
			}
			rose(p, s);
		}
		/* the lie and the shot or the player */
		readout(s, LIE_X, ROW_B, LIE_W, ball_lie_name(p->lie), -1, UIC_PAPER);
		n = MAP_X - 1 - WHO_X;
		if (n > BOX_W(8)) n = BOX_W(8);
		standing(p, t, (n - 3) / UI_CHAR);
		readout(s, WHO_X, ROW_B, n, t, 0, UIC_PAPER);
		if (p->state == PS_AIM) {
			button(s, MAP_X, ROW_B, MAP_W, "Map");
		} else if (swing) {
			/* the meter has the top row: the club goes under the rose's place */
			n = 0;
			add(t, &n, p->club == CLUB_PT ? "Putter" : ball_club_name(p->club));
			if (p->club != CLUB_PT) {
				add(t, &n, " ");
				n += play_num(t + n, mark_yards(p));
			}
			readout(s, MAP_X, ROW_B, s->lw - 2 - MAP_X, t, 0, UIC_PAPER);
		}
	}
	dirty(p, 0, (int16_t)(STRIP_Y * s->scale), s->w, (int16_t)(RENDER_STATUS * s->scale));
}

/* A fill of the meter kept to columns x0 to x1. */
static void SEC_UI mfill(Surface *s, int16_t x0, int16_t x1, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c)
{
	if (x < x0) { w -= x0 - x; x = x0; }
	if (x + w > x1) w = x1 - x;
	if (w > 0) ui_fill(s, x, y, w, h, c);
}

/* The meter in a well on the panel's top row, columns x0 to x1 only: the
 * track, the power so far in brass, quarter marks, the snap line and the
 * marker (black between white sides, so it shows on the track and the
 * power). The well's edge only on a full draw (x0 0); a frame's redraw
 * keeps to the rows inside and draws each pixel about once, to stay cheap
 * on the IIIx. */
static void SEC_UI draw_meter(Play *p, Surface *s, int16_t x0, int16_t x1)
{
	int16_t y = ROW_A, top = p->state == PS_UP ? p->mpos : p->turn, k, m = MTR_X + (p->mpos >> 4);
	int16_t wx = MTR_X - 2, ww = MTR_W + 4, sx = MTR_X + MTR_SNAP, i0 = x0, i1 = x1;

	if (x0 <= 0) {
		mfill(s, x0, x1, wx, y, ww, 1, UIC_INK);
		mfill(s, x0, x1, wx, y, 1, ROW_H, UIC_INK);
		mfill(s, x0, x1, wx + 1, y + ROW_H - 1, ww - 1, 1, UIC_WHITE);
		mfill(s, x0, x1, wx + ww - 1, y + 1, 1, ROW_H - 1, UIC_WHITE);
	}
	if (i0 < wx + 1) i0 = wx + 1;
	if (i1 > wx + ww - 1) i1 = wx + ww - 1;
	/* the well's colour above and below the track, and left of it */
	mfill(s, i0, i1, wx + 1, y + 1, ww - 2, 1, UIC_TITLE);
	mfill(s, i0, i1, wx + 1, y + 9, ww - 2, 1, UIC_TITLE);
	mfill(s, i0, i1, wx + 1, y + 2, 1, 7, UIC_TITLE);
	/* the track's edge, then inside it black, the power, black */
	mfill(s, i0, i1, MTR_X - 1, y + 2, MTR_W + 2, 1, UIC_PAPER_DK);
	mfill(s, i0, i1, MTR_X - 1, y + 8, MTR_W + 2, 1, UIC_PAPER_DK);
	mfill(s, i0, i1, MTR_X - 1, y + 3, 1, 5, UIC_PAPER_DK);
	mfill(s, i0, i1, MTR_X + MTR_W, y + 3, 1, 5, UIC_PAPER_DK);
	k = top > S16 ? (top - S16) >> 4 : 0;
	mfill(s, i0, i1, MTR_X, y + 3, MTR_SNAP, 5, UIC_BLACK);
	if (k) {
		mfill(s, i0, i1, sx, y + 3, k, 1, UIC_BRASS_LT);
		mfill(s, i0, i1, sx, y + 4, k, 3, UIC_BRASS);
		mfill(s, i0, i1, sx, y + 7, k, 1, UIC_BRASS_DK);
	}
	mfill(s, i0, i1, sx + k, y + 3, MTR_W - MTR_SNAP - k, 5, UIC_BLACK);
	for (k = 1; k < 4; k++) mfill(s, i0, i1, sx + (MTR_W - MTR_SNAP) * k / 4, y + 8, 1, 2, UIC_PAPER);
	mfill(s, i0, i1, sx, y + 1, 1, ROW_H - 2, UIC_TEXT_HI);
	if (p->club == CLUB_PT) {
		/* the putt that rolls to the cup on a flat green */
		fixed d = vec2_len(vec2_sub(pin(p), p->rv.ball));
		k = (int16_t)(sx + (int32_t)(MTR_W - MTR_SNAP) * (d >> 8) / (p->range >> 8));
		if (k > MTR_X + MTR_W - 1) k = MTR_X + MTR_W - 1;
		mfill(s, i0, i1, k - 1, y + 1, 3, 1, UIC_WHITE);
		mfill(s, i0, i1, k, y + 1, 1, ROW_H - 2, UIC_WHITE);
	}
	if (p->mpos >= 0) {
		mfill(s, i0, i1, m - 2, y + 1, 3, ROW_H - 2, UIC_WHITE);
		mfill(s, i0, i1, m - 1, y + 2, 1, ROW_H - 4, UIC_BLACK);
	}
	p->mshown = m;
	if (x0 <= 0) dirty(p, (int16_t)(x0 * s->scale), (int16_t)(y * s->scale), (int16_t)((x1 - x0) * s->scale), (int16_t)(ROW_H * s->scale));
	else dirty(p, (int16_t)(i0 * s->scale), (int16_t)((y + 1) * s->scale), (int16_t)((i1 - i0) * s->scale), (int16_t)((ROW_H - 2) * s->scale));
}

/* The hole's done in strokes: the score on the strip, holed or given
 * (gimme 1), or a computer golfer picked up (gimme 2). */
static void SEC_UI done(Play *p, uint8_t strokes, uint8_t gimme)
{
	int16_t n = 0, d = (int16_t)strokes - p->rv.hole->par;
	uint8_t many = p->round && p->round->players > 1;
	char *a = p->msg1, *b = p->msg2;

	/* with more than one player, whose score it is: "Player 2: 5 shots" and "Holed, Bogey" */
	if (many) {
		add(a, &n, p->round->pl[p->cur].name);
		add(a, &n, ": ");
	} else {
		/* the panel's line holds 19 characters */
		add(a, &n, gimme == 2 ? "Picked up, " : gimme ? "Gimme, " : strokes >= 10 ? "Holed, " : "In the cup, ");
	}
	/* msg1 holds 20 characters: the count shown stops at 99 */
	n += play_num(a + n, strokes > 99 ? 99 : strokes);
	add(a, &n, strokes == 1 ? " shot" : " shots");
	n = 0;
	b[0] = 0;
	if (gimme == 2) {
		add(b, &n, "Picked up");
	} else {
		if (many && strokes > 1) add(b, &n, gimme ? "Gimme, " : "Holed, ");
		if (strokes == 1) add(b, &n, "Hole in one!");
		else if (d <= -3) add(b, &n, "Albatross");
		else if (d == -2) add(b, &n, "Eagle");
		else if (d == -1) add(b, &n, "Birdie");
		else if (d == 0) add(b, &n, "Par");
		else if (d == 1) add(b, &n, "Bogey");
		else if (d == 2) add(b, &n, "Double bogey");
		else if (d == 3) add(b, &n, "Triple bogey");
		else {
			n += play_num(b + n, (uint32_t)d);
			add(b, &n, " over par");
		}
	}
	p->score = strokes;
	p->state = PS_DONE;
	p->wait = 1;
	p->redraw |= R_STRIP | R_OVER;
}

/* The result line when the ball has stopped. */
static void SEC_UI result(Play *p)
{
	const Shot *sh = &p->shot;
	int16_t n = 0;
	char *a = p->msg1, *b = p->msg2;

	a[0] = b[0] = 0;
	if (sh->result == SHOT_HOLED) {
		add(a, &n, "In the cup!");
	} else if (sh->lip || (sh->result == SHOT_OK && p->club == CLUB_PT && (sh->lie == LIE_GREEN || sh->lie == LIE_FRINGE))) {
		/* a putt: how far it's left in feet */
		vec2 d = vec2_sub(pin(p), sh->stop);
		add(a, &n, sh->lip ? "Lip out" : ball_lie_name(sh->lie));
		n = play_num(b, (uint32_t)((vec2_len(d) * 3 + FIX_HALF) >> 16));
		add(b, &n, " ft to the cup");
	} else if (sh->result == SHOT_WATER) {
		add(a, &n, "In the water");
		n = 0;
		add(b, &n, "1 stroke penalty");
	} else if (sh->result == SHOT_OOB) {
		add(a, &n, "Out of bounds");
		n = 0;
		add(b, &n, "Stroke and distance");
	} else if (sh->lie == LIE_GREEN) {
		vec2 d = vec2_sub(pin(p), sh->stop);
		add(a, &n, "On the green");
		n = play_num(b, (uint32_t)((vec2_len(d) + FIX_HALF) >> 16));
		add(b, &n, "y to pin");
	} else {
		add(a, &n, ball_lie_name(sh->lie));
		add(a, &n, ", ");
		n += play_num(a + n, (uint32_t)((sh->total + FIX_HALF) >> 16));
		add(a, &n, " yd");
		n = 0;
		if (p->mishit) {
			add(b, &n, "Mis-hit");
		} else {
			add(b, &n, "Carry ");
			n += play_num(b + n, (uint32_t)((sh->carry + FIX_HALF) >> 16));
			add(b, &n, " yd");
		}
	}
}

/* The shot counts: the hole is done if it's holed or given, or the ball
 * is where the shot left it, with any penalty. The round's player is kept
 * up to date. */
static void SEC_UI apply(Play *p)
{
	const Shot *sh = &p->shot;
	if (practice(p)) {
		/* the range: every ball from the tee; the green: putt out, then a new ball */
		RoundPlayer *rp = &p->round->pl[0];
		p->stroke = (uint8_t)(p->stroke + 1);
		p->rv.ball = sh->stop;
		p->lie = sh->lie;
		if (p->round->mode == MODE_RANGE || sh->result == SHOT_HOLED || (p->lie == LIE_GREEN && pin_feet(p) <= PLAY_GIMME_FT) ||
		    p->stroke > 9) {
			p->round->balls++;
			rp->taken = 0;
		} else {
			rp->ball = p->rv.ball;
			rp->lie = p->lie;
			rp->taken = (uint8_t)(p->stroke - 1);
		}
		return;
	}
	if (sh->result == SHOT_HOLED) {
		done(p, p->stroke, 0);
	} else {
		p->stroke = (uint8_t)(p->stroke + (sh->result == SHOT_OK ? 1 : 2));
		p->rv.ball = sh->stop;
		p->lie = sh->lie;
		if (p->lie == LIE_GREEN && pin_feet(p) <= PLAY_GIMME_FT) done(p, p->stroke, 1);
		/* a computer golfer picks up at 5 over par */
		else if (p->cpu && p->stroke > p->rv.hole->par + 4) done(p, (uint8_t)(p->rv.hole->par + 5), 2);
	}
	if (p->round) {
		RoundPlayer *rp = &p->round->pl[p->cur];
		if (p->state == PS_DONE) {
			rules_hole_out(p->round, p->cur, p->score);
		} else {
			rp->ball = p->rv.ball;
			rp->lie = p->lie;
			rp->taken = (uint8_t)(p->stroke - 1);
		}
	}
}

/* After the result: the score if the hole's done, or the next shot, which
 * may be another player's. Returns the PL_* flags, 0 if the view stays. */
static int SEC_UI next(Play *p)
{
	int n;
	if (p->applied) p->applied = 0;
	else apply(p);
	if (practice(p)) {
		if (p->round->mode == MODE_RANGE) {
			/* the same club, shape and aim for the next ball, over the scene as kept */
			uint8_t club = p->club, shape = p->shape;
			angle aim = p->aim, view = p->rv.aim;
			take(p, 0);
			set_club(p, club);
			if (ball_shape_ok(club, shape)) p->shape = shape;
			p->aim = aim;
			p->rv.aim = view;
			return PL_SCENE;
		}
		take(p, 0);
		return PL_RENDER;
	}
	if (p->state == PS_DONE) {
		int16_t d = (int16_t)p->score - p->rv.hole->par;
		/* the tunes are for people's scores */
		if (!p->cpu) snd_play(d < 0 ? SND_BIRDIE : d == 0 ? SND_HOLED : SND_BOGEY);
		return 0;
	}
	if (p->round && (n = rules_next(p->round, pin(p))) >= 0 && (n != p->cur || p->cpu)) {
		take(p, (uint8_t)n);
		return turn(p);
	}
	setup(p);
	return PL_RENDER;
}

#ifdef DEBUG
int play_debug_green(Play *p)
{
	vec2 to = pin(p);
	p->rv.ball = vec2_make(to.x - (2L << 16), to.y - (7L << 16));
	p->lie = LIE_GREEN;
	setup(p);
	return PL_RENDER;
}
#endif

void play_resized(Play *p)
{
	/* the pixels kept under the golfer are for the old size */
	if (p->golf) p->golf->w = 0;
	p->cam_ok = 0;
	p->npatch = 0;
	p->redraw |= R_ALL;
}

void play_settle(Play *p)
{
	Round *r = p->round;
	int n;
	if (!r) return;
	if (p->state == PS_FLY || p->state == PS_RESULT) {
		/* a computer golfer's result on the map carries on after the save, counted once */
		uint8_t live = p->cpu && p->view == PLAY_MAP && p->state == PS_RESULT;
		if (!p->applied) apply(p);
		p->applied = live;
		if (!live && p->state != PS_DONE) p->state = PS_AIM;
	}
	if (!r->over && rules_hole_over(r)) rules_advance(r);
	else if (!r->over && (n = rules_next(r, pin(p))) >= 0) r->turn = (uint8_t)n;
}

/* The golfer's pose for where the swing is: address while aiming, half way
 * back then the top as the meter goes up, the top on the way down and the
 * finish once the ball is struck. With the putter he stays at address. */
static void SEC_UI pose(Play *p, Surface *s, Surface *bg)
{
	RenderGolfer *g = p->golf;
	uint8_t want, ball = p->state <= PS_DOWN;
	int16_t x, y, w, h;

	if (!g || !g->w || p->rv.green) return;
	if (p->club == CLUB_PT || p->state == PS_AIM) want = POSE_ADDRESS;
	else if (p->state == PS_UP) want = p->mpos < (S16 + W16) / 2 ? POSE_BACK : POSE_TOP;
	else if (p->state == PS_DOWN) want = POSE_TOP;
	else want = POSE_FINISH;
	if (want == g->shown && ball == g->ball) return;
	if (render_pose(s, g, want, ball, &x, &y, &w, &h) < 0) return;
	if (bg) render_pose(bg, g, want, ball, &x, &y, &w, &h);
	g->shown = want;
	g->ball = ball;
	dirty(p, x, y, w, h);
	/* overlays over him come off the kept copy and go on again */
	p->redraw |= R_OVER;
}

/* Whether a computer golfer's wait is over: it starts on the first frame after wait was set. */
static uint8_t SEC_UI waited(Play *p, uint32_t now, uint32_t ms)
{
	if (p->wait) {
		p->tw = now;
		p->wait = 0;
	}
	return (uint32_t)(now - p->tw) >= ms;
}

/* The map showing a quick computer golfer's result: on to the next shot,
 * then their score, then whoever is next. */
static int SEC_UI map_frame(Play *p, uint32_t now)
{
	int f;
	if (!p->cpu || (p->state != PS_RESULT && p->state != PS_DONE)) return 0;
	if (!waited(p, now, PLAY_CPU_MS)) return PL_ANIM;
	if (p->state == PS_DONE) return after_done(p);
	f = next(p);
	if (f) return f;
	/* holed: the score on the panel */
	return PL_PANEL | PL_ANIM;
}

int play_frame(Play *p, Surface *s, Surface *bg, uint32_t now)
{
	int flags = 0;
	uint8_t k;

	if (p->view == PLAY_MAP) return map_frame(p, now);
	if (p->view != PLAY_SCENE) return 0;
	camera(p, s);
	p->dirty.w = 0;
	if (p->redraw & R_ALL) {
		p->npatch = 0;
		p->redraw = R_STRIP | R_OVER;
		dirty(p, 0, 0, s->w, s->h);
	}

	switch (p->state) {
	case PS_AIM:
		/* a computer golfer swings once the scene has shown a moment */
		if (p->cpu) {
			if (waited(p, now, PLAY_CPU_AIM_MS)) launch(p);
			flags |= PL_ANIM;
		}
		break;
	case PS_DONE:
		if (p->cpu) {
			if (waited(p, now, PLAY_CPU_MS)) return after_done(p);
			flags |= PL_ANIM;
		}
		break;
	case PS_UP:
		p->mpos = meter_pos(p, now);
		if (p->mpos >= W16) {
			/* no second click: full power, and it starts back down (a putt goes) */
			p->mpos = p->turn = W16;
			p->t1 = p->t0 + MTR_UP_MS;
			p->power = FIX_ONE;
			if (p->club == CLUB_PT) launch(p);
			else p->state = PS_DOWN;
		}
		p->redraw |= R_METER;
		flags |= PL_ANIM;
		break;
	case PS_DOWN:
		p->mpos = meter_pos(p, now);
		p->redraw |= R_METER;
		if (p->mpos <= 0) {
			/* no third click: a mis-hit */
			p->mpos = 0;
			p->mishit = 1;
			p->snap = -FIX_ONE;
			launch(p);
		}
		flags |= PL_ANIM;
		break;
	case PS_FLY: {
		int32_t e, fly = (int32_t)p->shot.fly_n * FLY_MS;
		int16_t i;
		if (p->shown == -2) {
			p->tf = now;
			p->shown = -1;
		}
		e = (int32_t)(now - p->tf);
		{
			int32_t k = e < fly ? e / FLY_MS : p->shot.fly_n + (e - fly) / ROLL_MS;
			if (k > p->shot.n - 1) k = p->shot.n - 1;
			i = (int16_t)k;
		}
		landed(p, i);
		if (i >= p->shot.n - 1) {
			i = (int16_t)(p->shot.n - 1);
			stopped(p);
			result(p);
			p->state = PS_RESULT;
			p->t0 = now;
			p->redraw |= R_STRIP;
		}
		if (i != p->shown) {
			p->shown = i;
			p->redraw |= R_OVER;
		}
		flags |= PL_ANIM;
		break;
	}
	case PS_RESULT:
		if ((int32_t)(now - p->t0) >= (p->cpu ? PLAY_CPU_MS : RESULT_MS)) {
			int f = next(p);
			if (f) return f;
			break;
		}
		flags |= PL_ANIM;
		break;
	}

	pose(p, s, bg);
	if (p->redraw & R_OVER) {
		int16_t mx = 0, my = 0, ok = 0;
		if (p->state <= PS_DOWN) {
			ok = mark_at(p, p->aim, &mx, &my);
			if (p->state == PS_AIM && (!ok || mx < p->cam.cx - AIM_EDGE * s->scale || mx > p->cam.cx + AIM_EDGE * s->scale)) {
				/* aimed off the side of the view: draw it again looking that way */
				p->rv.aim = p->aim;
				p->cam_ok = 0;
				p->redraw = R_ALL;
				return PL_RENDER;
			}
		}
		for (k = 0; k < p->npatch; k++) {
			URect *r = &p->patch[k];
			if (bg) gfx_copy(s, bg, r->x, r->y, r->w, r->h);
			dirty(p, r->x, r->y, r->w, r->h);
		}
		p->npatch = 0;
		if (ok && p->rv.green) aim_dots(p, s);
		else if (ok) draw_mark(p, s, mx, my);
		if (p->state >= PS_FLY && p->shown >= 0) {
			draw_ball(p, s);
		} else if (p->rv.green) {
			/* the green view leaves the ball to us */
			ball_at(p, s, p->rv.ball.x, p->rv.ball.y, hole_height(p->rv.hole, p->rv.ball.x, p->rv.ball.y));
		}
	}
	if (p->redraw & R_STRIP) draw_strip(p, s);
	if ((p->redraw & R_STRIP) && (p->state == PS_UP || p->state == PS_DOWN)) {
		draw_meter(p, s, 0, s->lw);
	} else if (p->redraw & R_METER) {
		/* only between where the marker was and where it is, as the power fill follows it */
		int16_t m = MTR_X + (p->mpos >> 4), a = m < p->mshown ? m : p->mshown, b = m < p->mshown ? p->mshown : m;
		draw_meter(p, s, a - 3, b + 3);
	}
	p->redraw = 0;
	if (p->dirty.w > 0) flags |= PL_DIRTY;
	return flags;
}

int16_t play_num(char *t, uint32_t v)
{
	char b[10];
	int16_t n = 0, k = 0;

	do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 10);
	while (n) t[k++] = b[--n];
	t[k] = 0;
	return k;
}

/* The panel's top row on the map and while a scene draws: hole, par and yards. */
static void SEC_UI hole_row(Surface *s, const Hole *h)
{
	char t[12];
	int16_t n = 0;

	face(s);
	add(t, &n, "Hole ");
	play_num(t + n, h->number);
	readout(s, 2, ROW_A, BOX_W(7), t, -1, UIC_PAPER);
	n = 0;
	add(t, &n, "Par ");
	play_num(t + n, h->par);
	readout(s, 3 + BOX_W(7), ROW_A, BOX_W(5), t, -1, UIC_PAPER);
	n = play_num(t, h->tees[0].yards);
	add(t, &n, " yd");
	readout(s, 4 + BOX_W(7) + BOX_W(5), ROW_A, BOX_W(6), t, 1, UIC_TEXT_HI);
}

void play_status(Surface *s, const Play *p, const char *info)
{
	int16_t w = s->lw - 4;

	if (p->cpu && p->state == PS_RESULT) {
		/* a computer golfer's shot: who and which shot, and where it went */
		char t[24];
		int16_t n = 0;
		face(s);
		add(t, &n, p->round->pl[p->cur].name);
		add(t, &n, ", shot ");
		play_num(t + n, p->stroke);
		readout(s, 2, ROW_A, s->lw - 4, t, -1, UIC_PAPER);
		readout(s, 2, ROW_B, s->lw - 4, p->msg1, -1, UIC_TEXT_HI);
		return;
	}
	if (p->cpu && p->state == PS_DONE) {
		face(s);
		well(s, 2, ROW_A, s->lw - 4, 2 * ROW_H + 1);
		box_text(s, 2, ROW_A, s->lw - 4, p->msg1, -1, 0, UIC_TEXT_HI);
		box_text(s, 2, ROW_B, s->lw - 4, p->msg2, -1, 0, UIC_PAPER);
		return;
	}
	hole_row(s, p->rv.hole);
	if (!info) info = p->rv.hole->name;
	if (p->round && !practice(p)) {
		w = CARD_X - 3;
		button(s, CARD_X, ROW_B, CARD_W, "Card");
	}
	readout(s, 2, ROW_B, w, info, -1, UIC_PAPER);
}

void play_turn(Surface *s, const Play *p)
{
	char t[24];
	int16_t n = 0;

	hole_row(s, p->rv.hole);
	if (practice(p)) {
		/* "Range, ball 3" */
		add(t, &n, p->round->mode == MODE_RANGE ? "Range, ball " : "Green, ball ");
		play_num(t + n, (uint32_t)p->round->balls + 1);
	} else {
		add(t, &n, p->round ? p->round->pl[p->cur].name : "Player 1");
		add(t, &n, ", shot ");
		play_num(t + n, p->stroke);
	}
	readout(s, 2, ROW_B, s->lw - 4, t, -1, UIC_PAPER);
}

void play_card(Surface *s, const Play *p)
{
	Surface c = *s;
	/* the panel with the hole under it, and the card in a 160 unit square in the middle */
	if (p->rv.hole) {
		hole_row(s, p->rv.hole);
		readout(s, 2, ROW_B, s->lw - 4, p->rv.hole->name, -1, UIC_PAPER);
	}
	gfx_centre(&c, 1);
	if (p->round && p->board) board_draw(&c, p->round, p->card, p->csel);
	else if (p->round) card_draw(&c, p->round, p->card, p->page, p->csel);
}

void play_map(Surface *s, const Play *p)
{
	RenderMap m;
	char t[12];
	int16_t n, x, y, sc = s->scale, k;

	if (render_map(s, &p->rv, &m) != 0) return;
	/* the last shot's path, dotted */
	if (p->have_shot) {
		for (k = 0; k < p->shot.n; k += 2) {
			const BallPt *b = &p->shot.path[k];
			x = (int16_t)(m.left + ((fixed)b->x * 4096) / m.ypp);
			y = (int16_t)(m.top + (m.length - ((fixed)b->y * 4096)) / m.ypp);
			if (y < (STRIP_Y - 1) * sc) gfx_fill(s, x, y, sc, sc, UIC_WHITE);
		}
	}
	n = play_num(t, m.yards);
	t[n++] = 'y';
	t[n] = 0;
	/* the yardage in a black box beside the middle of the line, kept on screen */
	x = (int16_t)((m.bx + m.px) / 2 / sc + 4);
	y = (int16_t)((m.by + m.py) / 2 / sc - UI_CHAR / 2);
	if (x + n * UI_CHAR > s->lw - 1) x = (int16_t)((m.bx + m.px) / 2 / sc - 4 - n * UI_CHAR);
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (y > STRIP_Y - UI_CHAR) y = STRIP_Y - UI_CHAR;
	ui_fill(s, x - 1, y - 1, n * UI_CHAR + 2, UI_CHAR + 2, UIC_BLACK);
	ui_text(s, x, y, t, n, UIC_WHITE, -1);
}
