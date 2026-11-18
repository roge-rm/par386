#include "play.h"

/* What play_frame draws again. */
#define R_STRIP  1
#define R_OVER   2      /* the aim marker or the ball moved */
#define R_METER  4
#define R_ALL    8      /* the scene under the overlays is new */

#define STRIP_Y   (160 - RENDER_STATUS)
#define FLY_MS    42    /* a path point in the air: 1.5 times real time */
#define ROLL_MS   62    /* a point on the ground: twice real time */
#define RESULT_MS 1500
#define AIM_STEP  182   /* a degree */
#define AIM_EDGE  56    /* the marker this far off the centre draws the scene again */
#define MAP_X     136   /* the Map button on the strip's second line */
#define CARD_X    128   /* and the Card button on the map's */
#define S16       (MTR_SNAP * 16)
#define W16       (MTR_W * 16)

/* Wind arrows, 7 x 7, by the way it blows on screen: up (with the shot),
 * up-left, left and on round. */
static const uint8_t arrows[8][7] = {
	{ 0x10, 0x38, 0x54, 0x10, 0x10, 0x10, 0x10 },
	{ 0xF0, 0xC0, 0xA0, 0x90, 0x08, 0x04, 0x02 },
	{ 0x00, 0x20, 0x40, 0xFE, 0x40, 0x20, 0x00 },
	{ 0x02, 0x04, 0x08, 0x90, 0xA0, 0xC0, 0xF0 },
	{ 0x10, 0x10, 0x10, 0x10, 0x54, 0x38, 0x10 },
	{ 0x80, 0x40, 0x20, 0x12, 0x0A, 0x06, 0x1E },
	{ 0x00, 0x08, 0x04, 0xFE, 0x04, 0x08, 0x00 },
	{ 0x1E, 0x06, 0x0A, 0x12, 0x20, 0x40, 0x80 },
};

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

/* Yards to the aim marker: the club's carry, or for the putter the pin. */
static uint16_t SEC_UI mark_yards(const Play *p)
{
	uint16_t d;
	if (p->club != CLUB_PT) return ball_carry(p->club, p->lie);
	d = pin_yards(p);
	return d > BALL_PUTT_MAX ? BALL_PUTT_MAX : d < 1 ? 1 : d;
}

/* Sets the club; the putter goes with the green view and a meter scaled
 * to half as far again as the cup, 3 to BALL_PUTT_MAX yards. */
static void SEC_UI set_club(Play *p, uint8_t club)
{
	fixed r;
	p->club = club;
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
	set_club(p, p->lie == LIE_GREEN || p->lie == LIE_FRINGE ? CLUB_PT : ball_pick(pin_yards(p), p->lie));
	p->state = PS_AIM;
	p->cam_ok = 0;
	p->redraw = R_ALL;
	p->shown = -2;
	p->mishit = 0;
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
	ball_wind(seed, h->number, &p->wind_mph, &p->wind_dir);
	setup(p);
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
	} else {
		p->rv.ball = p->rv.hole->tees[0].pos;
		p->lie = LIE_TEE;
	}
	p->stroke = (uint8_t)(rp->taken + 1);
	p->have_shot = 0;
	p->view = PLAY_SCENE;
	setup(p);
}

void play_start(Play *p, const Hole *h, const SurfaceGrid *g, Round *r)
{
	int n;

	play_init(p, h, g, r->seed);
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
	ball_shot(&in, &p->shot);
	p->have_shot = 1;
	p->state = PS_FLY;
	p->shown = -2;
	p->redraw |= R_OVER;
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
		return PL_RENDER | PL_TURN;
	}
	rules_advance(r);
	return open_card(p, r->over ? CARD_FINAL : CARD_HOLE);
}

/* The scorecard's button i. */
static int SEC_UI card_button(Play *p, uint8_t i)
{
	if (i == 1) {
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
	uint8_t pages = card_pages(p->round);
	switch (key) {
	case KEY_LEFT: case KEY_RIGHT:
		if (pages < 2) return 0;
		p->csel ^= 1;
		return PL_CARD;
	case KEY_UP:
		if (pages < 2) return 0;
		p->page = (uint8_t)((p->page + pages - 1) % pages);
		return PL_CARD;
	case KEY_DOWN:
		if (pages < 2) return 0;
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

int play_key(Play *p, uint8_t key, uint32_t now)
{
	if (key == KEY_BACK) return PL_BACK;
	if (p->view == PLAY_CARD) return card_key(p, key);
	if (key == KEY_CARD && p->round && (p->state == PS_AIM || p->view == PLAY_MAP)) return open_card(p, CARD_VIEW);
	if (p->view == PLAY_MAP) {
		p->view = PLAY_SCENE;
		p->redraw = R_ALL;
		return PL_SCENE;
	}
	if (p->state == PS_DONE) return after_done(p);
	if (key == KEY_SELECT) return click(p, now);
	if (p->state != PS_AIM) return 0;
	switch (key) {
	case KEY_OTHER:
		p->view = PLAY_MAP;
		return PL_MAP;
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

int play_tap(Play *p, int16_t x, int16_t y, uint32_t now)
{
	if (p->view == PLAY_CARD) {
		int i = card_hit(p->round, p->card, p->page, x, y);
		return i < 0 ? 0 : card_button(p, (uint8_t)i);
	}
	if (p->view == PLAY_MAP) {
		if (p->round && y >= STRIP_Y + UI_CHAR && x >= CARD_X) return open_card(p, CARD_VIEW);
		return play_key(p, KEY_SELECT, now);
	}
	if (p->state == PS_DONE) return after_done(p);
	if (p->state == PS_UP || p->state == PS_DOWN || p->state == PS_RESULT) return click(p, now);
	if (y >= STRIP_Y) {
		if (x >= MAP_X && y >= STRIP_Y + UI_CHAR) return play_key(p, KEY_OTHER, now);
		if (p->state == PS_AIM) return click(p, now);
		return 0;
	}
	/* cx is 80 times the scale */
	if (p->state == PS_AIM && p->cam_ok) aim_at(p, (int16_t)(x * (p->cam.cx / 80)));
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
	int16_t vh = (int16_t)(STRIP_Y * (p->cam.cx / 80));
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

static void SEC_UI pad(char *t, int16_t *n, int16_t to)
{
	while (*n < to) t[(*n)++] = ' ';
	t[*n] = 0;
}

static void SEC_UI line(Surface *s, int16_t row, const char *t)
{
	int16_t n;
	for (n = 0; t[n] && n < 20; n++) {}
	ui_text(s, 0, STRIP_Y + row * UI_CHAR, t, n, UIC_WHITE, -1);
}

/* The strip's second line: the lie and the shot, or with more than one
 * player, whose turn it is and the lie. */
static void SEC_UI lie_line(const Play *p, Surface *s)
{
	char t[24];
	int16_t n = 0;

	if (p->round && p->round->players > 1) {
		add(t, &n, p->round->pl[p->cur].name);
		pad(t, &n, RULES_NAME_MAX + 1);
		add(t, &n, ball_lie_name(p->lie));
	} else {
		add(t, &n, ball_lie_name(p->lie));
		pad(t, &n, 9);
		add(t, &n, "Shot ");
		n += play_num(t + n, p->stroke);
	}
	line(s, 1, t);
}

static void SEC_UI draw_strip(Play *p, Surface *s)
{
	char t[24];
	int16_t n = 0;

	ui_fill(s, 0, STRIP_Y, 160, RENDER_STATUS, UIC_BLACK);
	if (p->state == PS_RESULT || p->state == PS_DONE) {
		line(s, 0, p->msg1);
		line(s, 1, p->msg2);
	} else if (p->club == CLUB_PT) {
		/* the putter and the cup in feet, no wind on the green */
		add(t, &n, "Putter  Cup ");
		n += play_num(t + n, pin_feet(p));
		add(t, &n, " ft");
		line(s, 0, t);
		lie_line(p, s);
	} else {
		/* club and carry, the pin, the wind */
		add(t, &n, ball_club_name(p->club));
		if (p->club != CLUB_PT) {
			add(t, &n, " ");
			n += play_num(t + n, ball_carry(p->club, p->lie));
		}
		pad(t, &n, 8);
		add(t, &n, "Pin ");
		n += play_num(t + n, pin_yards(p));
		pad(t, &n, 17);
		if (p->wind_mph < 10) add(t, &n, " ");
		n += play_num(t + n, p->wind_mph);
		line(s, 0, t);
		if (p->wind_mph) {
			const uint8_t *a = arrows[((uint16_t)(p->wind_dir - p->aim) + 4096) >> 13 & 7];
			int16_t i, j;
			for (j = 0; j < 7; j++)
				for (i = 0; i < 7; i++)
					if (a[j] & (0x80 >> i)) ui_fill(s, 16 * UI_CHAR + i, STRIP_Y + j, 1, 1, UIC_WHITE);
		}
		lie_line(p, s);
	}
	if (p->state == PS_AIM) {
		ui_fill(s, MAP_X, STRIP_Y + UI_CHAR, 160 - MAP_X, UI_CHAR, UIC_WHITE);
		ui_text(s, MAP_X, STRIP_Y + UI_CHAR, "Map", 3, UIC_BLACK, -1);
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

/* The meter on the strip's first line, columns x0 to x1 only: the bar, the
 * power so far, quarter marks, the snap line and the marker (black between
 * white sides, so it shows on the bar and the power). */
static void SEC_UI draw_meter(Play *p, Surface *s, int16_t x0, int16_t x1)
{
	int16_t y = STRIP_Y, top = p->state == PS_UP ? p->mpos : p->turn, k, m = MTR_X + (p->mpos >> 4);

	mfill(s, x0, x1, 0, y, 160, UI_CHAR, UIC_BLACK);
	mfill(s, x0, x1, MTR_X - 1, y + 1, MTR_W + 2, 6, UIC_WHITE);
	mfill(s, x0, x1, MTR_X, y + 2, MTR_W, 4, UIC_BLACK);
	if (top > S16) mfill(s, x0, x1, MTR_X + MTR_SNAP, y + 2, (top - S16) >> 4, 4, UIC_BALL_LT);
	for (k = 1; k < 4; k++) mfill(s, x0, x1, MTR_X + MTR_SNAP + (MTR_W - MTR_SNAP) * k / 4, y + 6, 1, 2, UIC_WHITE);
	mfill(s, x0, x1, MTR_X + MTR_SNAP, y, 1, UI_CHAR, UIC_WHITE);
	if (p->club == CLUB_PT) {
		/* the putt that rolls to the cup on a flat green */
		fixed d = vec2_len(vec2_sub(pin(p), p->rv.ball));
		k = (int16_t)(MTR_X + MTR_SNAP + (int32_t)(MTR_W - MTR_SNAP) * (d >> 8) / (p->range >> 8));
		if (k > MTR_X + MTR_W - 1) k = MTR_X + MTR_W - 1;
		mfill(s, x0, x1, k - 1, y, 3, 1, UIC_WHITE);
		mfill(s, x0, x1, k, y, 1, UI_CHAR, UIC_WHITE);
	}
	if (p->mpos >= 0) {
		mfill(s, x0, x1, m - 2, y, 3, UI_CHAR, UIC_WHITE);
		mfill(s, x0, x1, m - 1, y + 1, 1, UI_CHAR - 2, UIC_BLACK);
	}
	p->mshown = m;
	dirty(p, (int16_t)(x0 * s->scale), (int16_t)(y * s->scale), (int16_t)((x1 - x0) * s->scale), (int16_t)(UI_CHAR * s->scale));
}

/* The hole's done in strokes: the score on the strip, holed or given. */
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
		add(a, &n, gimme ? "Gimme, " : "In the cup, ");
	}
	/* msg1 holds 20 characters: the count shown stops at 99 */
	n += play_num(a + n, strokes > 99 ? 99 : strokes);
	add(a, &n, strokes == 1 ? " shot" : " shots");
	n = 0;
	b[0] = 0;
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
	p->score = strokes;
	p->state = PS_DONE;
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
	if (sh->result == SHOT_HOLED) {
		done(p, p->stroke, 0);
	} else {
		p->stroke = (uint8_t)(p->stroke + (sh->result == SHOT_OK ? 1 : 2));
		p->rv.ball = sh->stop;
		p->lie = sh->lie;
		if (p->lie == LIE_GREEN && pin_feet(p) <= PLAY_GIMME_FT) done(p, p->stroke, 1);
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
	apply(p);
	if (p->state == PS_DONE) return 0;
	if (p->round && (n = rules_next(p->round, pin(p))) >= 0 && n != p->cur) {
		take(p, (uint8_t)n);
		return PL_RENDER | PL_TURN;
	}
	setup(p);
	return PL_RENDER;
}

void play_settle(Play *p)
{
	Round *r = p->round;
	int n;
	if (!r) return;
	if (p->state == PS_FLY || p->state == PS_RESULT) {
		apply(p);
		if (p->state != PS_DONE) p->state = PS_AIM;
	}
	if (!r->over && rules_hole_over(r)) rules_advance(r);
	else if (!r->over && (n = rules_next(r, pin(p))) >= 0) r->turn = (uint8_t)n;
}

int play_frame(Play *p, Surface *s, const Surface *bg, uint32_t now)
{
	int flags = 0;
	uint8_t k;

	if (p->view != PLAY_SCENE) return 0;
	camera(p, s);
	p->dirty.w = 0;
	if (p->redraw & R_ALL) {
		p->npatch = 0;
		p->redraw = R_STRIP | R_OVER;
		dirty(p, 0, 0, s->w, s->h);
	}

	switch (p->state) {
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
		if (i >= p->shot.n - 1) {
			i = (int16_t)(p->shot.n - 1);
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
		if ((int32_t)(now - p->t0) >= RESULT_MS) {
			int f = next(p);
			if (f) return f;
			break;
		}
		flags |= PL_ANIM;
		break;
	}

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
		draw_meter(p, s, 0, 160);
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

void play_status(Surface *s, const Play *p, const char *info)
{
	const Hole *h = p->rv.hole;
	char t[24];
	int16_t n = 0;

	ui_fill(s, 0, STRIP_Y, 160, RENDER_STATUS, UIC_BLACK);
	add(t, &n, "Hole ");
	n += play_num(t + n, h->number);
	add(t, &n, " Par ");
	n += play_num(t + n, h->par);
	add(t, &n, " ");
	n += play_num(t + n, h->tees[0].yards);
	add(t, &n, " yd");
	line(s, 0, t);
	if (!info) info = h->name;
	if (p->round) {
		/* the Card button at the right */
		for (n = 0; info[n] && n < CARD_X / UI_CHAR - 1; n++) {}
		ui_text(s, 0, STRIP_Y + UI_CHAR, info, n, UIC_WHITE, -1);
		ui_fill(s, CARD_X, STRIP_Y + UI_CHAR, 160 - CARD_X, UI_CHAR, UIC_WHITE);
		ui_text(s, CARD_X, STRIP_Y + UI_CHAR, "Card", 4, UIC_BLACK, -1);
	} else {
		line(s, 1, info);
	}
}

void play_turn(Surface *s, const Play *p)
{
	const Hole *h = p->rv.hole;
	char t[24];
	int16_t n = 0;

	ui_fill(s, 0, STRIP_Y, 160, RENDER_STATUS, UIC_BLACK);
	add(t, &n, "Hole ");
	n += play_num(t + n, h->number);
	add(t, &n, " Par ");
	n += play_num(t + n, h->par);
	add(t, &n, " ");
	n += play_num(t + n, h->tees[0].yards);
	add(t, &n, " yd");
	line(s, 0, t);
	n = 0;
	if (p->round) add(t, &n, p->round->pl[p->cur].name);
	else add(t, &n, "Player 1");
	add(t, &n, ", shot ");
	n += play_num(t + n, p->stroke);
	line(s, 1, t);
}

void play_card(Surface *s, const Play *p)
{
	if (p->round) card_draw(s, p->round, p->card, p->page, p->csel);
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
	if (x + n * UI_CHAR > 159) x = (int16_t)((m.bx + m.px) / 2 / sc - 4 - n * UI_CHAR);
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (y > STRIP_Y - UI_CHAR) y = STRIP_Y - UI_CHAR;
	ui_fill(s, x - 1, y - 1, n * UI_CHAR + 2, UI_CHAR + 2, UIC_BLACK);
	ui_text(s, x, y, t, n, UIC_WHITE, -1);
}
