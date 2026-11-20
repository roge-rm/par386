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
#define MAP_AIM_STEP 910  /* five degrees */
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
#define ACTION_X  2
#define ACTION_W  BOX_W(5)
#define AIM_LIE_X (ACTION_X + ACTION_W)
#define CLUB_PICK_X  8
#define CLUB_PICK_Y  29
#define CLUB_PICK_W  72
#define CLUB_PICK_H  12
#define CLUB_PICK_COLS 2
#define CLUB_PICK_ROWS ((CLUB_COUNT + CLUB_PICK_COLS) / CLUB_PICK_COLS)
#define MAP_W     BOX_W(3)          /* the Map button, left of the rose */
#define MAP_X     (ROSE_X - 1 - MAP_W)
#define AIM_MAP_X (p->club == CLUB_PT ? s->lw - 2 - MAP_W : MAP_X)
#define REPEAT_W  (BOX_W(6) + 6)
#define REPEAT_X  (AIM_MAP_X - 1 - REPEAT_W)
#define CARD_W    BOX_W(4)          /* the Card button on the map's panel */
#define CARD_X    (s->lw - 2 - CARD_W)
#define MAP_CARD_X (ROSE_X - 1 - CARD_W)
#define MAP_BACK_X (MAP_CARD_X - 1 - CARD_W)
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
	vec2 cup = pin(p);
	fixed d = vec2_len(vec2_sub(cup, p->rv.ball));
	fixed r = ((d + (d >> 1) + FIX_ONE - 1) >> 16) << 16;
	p->club = club;
	if (!ball_shape_ok(club, p->shape)) p->shape = SHAPE_NORMAL;
	p->rv.green = club == CLUB_PT;
	if (r < (3L << 16)) r = (3L << 16);
	if (r > ((fixed)BALL_PUTT_MAX << 16)) r = (fixed)BALL_PUTT_MAX << 16;
	p->range = r;
	if (p->rv.green) {
		const Hole *h = p->rv.hole;
		fixed rise = hole_height(h, cup.x, cup.y) - hole_height(h, p->rv.ball.x, p->rv.ball.y);
		/* The meter uses 8.8 yards, which also leaves room for extreme grades. */
		int32_t pace = (hole_putt_len(h, p->rv.grid, p->rv.ball, cup, d) >> 8) + (rise >> 8) * BALL_PUTT_GRADE;
		if (pace < 0) pace = 0;
		if (pace >= (r >> 8)) p->putt_tick = MTR_W - MTR_SNAP - 1;
		else p->putt_tick = (uint8_t)((MTR_W - MTR_SNAP) * pace / (r >> 8));
	}
}

static int SEC_MENU choose_club(Play *p, uint8_t club)
{
	uint8_t green = p->rv.green;
	if (p->lie == LIE_GREEN) return 0;
	set_club(p, club);
	p->redraw |= R_OVER | R_STRIP;
	if (p->rv.green == green) return 0;
	p->rv.aim = p->aim;
	p->cam_ok = 0;
	p->redraw = R_ALL;
	return PL_RENDER;
}

static int SEC_MENU dismiss_clubs(Play *p, int club)
{
	int f = club < CLUB_COUNT ? choose_club(p, (uint8_t)club) : 0;
	p->view = PLAY_SCENE;
	p->redraw = R_ALL;
	return f ? f : PL_SCENE;
}

static int SEC_MENU club_key(Play *p, uint8_t key)
{
	int16_t sel = p->csel;
	switch (key) {
	case KEY_BACK: return dismiss_clubs(p, CLUB_COUNT);
	case KEY_SELECT: return dismiss_clubs(p, sel);
	case KEY_LEFT: sel--; break;
	case KEY_RIGHT: sel++; break;
	case KEY_UP: sel -= CLUB_PICK_COLS; break;
	case KEY_DOWN: sel += CLUB_PICK_COLS; break;
	default: return 0;
	}
	if (sel < 0) sel = 0;
	if (sel > CLUB_COUNT) sel = CLUB_COUNT;
	p->csel = (uint8_t)sel;
	return PL_CARD;
}

static int SEC_MENU club_tap(Play *p, int16_t x, int16_t y)
{
	x -= CLUB_PICK_X;
	y -= CLUB_PICK_Y;
	if (x < 0 || x >= CLUB_PICK_COLS * CLUB_PICK_W ||
	    y < 0 || y >= CLUB_PICK_ROWS * CLUB_PICK_H) return 0;
	if (x % CLUB_PICK_W == CLUB_PICK_W - 1 || y % CLUB_PICK_H >= ROW_H) return 0;
	return dismiss_clubs(p, y / CLUB_PICK_H * CLUB_PICK_COLS + x / CLUB_PICK_W);
}

/* Gets ready for the next shot from rv.ball: aim, club and state. On the
 * green and the fringe it's the putter. */
static void SEC_UI setup(Play *p)
{
	const Hole *h = p->rv.hole;
	vec2 to = pin(p);

	if (p->rv.ball.x == h->tees[h->tee].pos.x && p->rv.ball.y == h->tees[h->tee].pos.y) to = h->aim;
	p->rv.aim = vec2_angle(vec2_sub(to, p->rv.ball));
	p->aim = p->rv.aim;
	p->shape = SHAPE_NORMAL;
	set_club(p, p->lie == LIE_GREEN || p->lie == LIE_FRINGE ? CLUB_PT : ball_pick(pin_yards(p), p->lie));
	p->state = PS_AIM;
	p->target_range = 0;
	p->target_set = 0;
	p->cam_ok = 0;
	p->redraw = R_ALL;
	p->shown = -2;
	p->mishit = 0;
	if (p->onmap) {
		/* the shot was on the map: the next one starts in the scene */
		p->onmap = 0;
		p->view = PLAY_SCENE;
	}
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
	p->rv.ball = h->tees[h->tee].pos;
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

static uint8_t SEC_UI practice_green(const Play *p)
{
	return p->round && p->round->mode == MODE_GREEN;
}

/* Gimmies use the same rounded distance as the panel. */
static uint8_t SEC_UI given(const Play *p, vec2 ball, uint8_t lie)
{
	return lie == LIE_GREEN && (!p->round || !p->round->holeout) &&
	       ((vec2_len(vec2_sub(pin(p), ball)) * 3 + FIX_HALF) >> 16) <= PLAY_GIMME_FT;
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
	p->rv.player = i;
	r->turn = i;
	if (rp->taken) {
		p->rv.ball = rp->ball;
		p->lie = rp->lie;
	} else if (r->mode == MODE_GREEN) {
		green_spot(p);
	} else {
		p->rv.ball = p->rv.hole->tees[p->rv.hole->tee].pos;
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

void play_start(Play *p, const Hole *h, const SurfaceGrid *g, Round *r)
{
	int n;
	uint8_t quick = p->quick, mapview = p->mapview;

	play_init(p, h, g, rules_round_seed(r));
	p->quick = quick;
	p->mapview = mapview;
	p->round = r;
	if (r->tour_stage == TOUR_BREAK) {
		open_card(p, CARD_FINAL);
		p->board = 1;
		return;
	}
	/* r->turn may still be the player who just hit; the rules pick who is next */
	n = rules_next(r, pin(p));
	take(p, (uint8_t)(n < 0 ? 0 : n));
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
	p->result_pending = 0;
	p->mprep = 0;
	p->shown = -2;
	p->redraw |= R_STRIP | R_OVER;
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

	p->onmap = 0;
	if (!r) return PL_BACK;
	n = rules_next(r, pin(p));
	if (n >= 0) {
		take(p, (uint8_t)n);
		return turn(p);
	}
	rules_advance(r);
	n = open_card(p, r->over || r->tour_stage == TOUR_BREAK ? CARD_FINAL : CARD_HOLE);
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
	if (p->card == CARD_FINAL && p->round->tour_stage == TOUR_BREAK) {
		rules_tour_next(p->round);
		return PL_NEXT;
	}
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
		p->mfirst = 2;
		p->autoturn = 0;
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
		uint32_t ts;
		int32_t err, a;
		fixed e;
		if (p->autoturn && (uint32_t)(now - p->tg) < MTR_GRACE_MS) {
			/* just after the meter turned back by itself: the power press at full, and the way down starts now */
			p->autoturn = 0;
			p->t1 = now;
			p->mpos = W16;
			p->redraw |= R_METER;
			snd_play(SND_CLICK);
			return PL_ANIM;
		}
		p->autoturn = 0;
		/* when the marker crossed (or will cross) the snap line, and how far off the click was */
		ts = p->t1 + (uint32_t)((int32_t)(p->turn - S16) * MTR_DOWN_MS / (W16 - S16));
		err = (int32_t)(now - ts);
		a = err < 0 ? -err : err;
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

/* A key or tap while a shot is on the map: the result goes on sooner, the
 * score goes on; nothing while it flies. */
static int SEC_UI map_click(Play *p, uint32_t now)
{
	if (p->cpu) return hurry(p, now);
	if (p->state == PS_RESULT) {
		p->t0 = now - RESULT_MS;
		return PL_ANIM;
	}
	if (p->state == PS_DONE) return after_done(p);
	return 0;
}

/* Keeps the target on the hole along the current bearing. */
static void SEC_MENU map_point(Play *p)
{
	const Hole *h = p->rv.hole;
	vec2 ball = p->rv.ball;
	fixed range = p->target_range;
	int16_t dx = icos(p->aim), dy = isin(p->aim);
	fixed x = fix_mulq14(range, dx), y = fix_mulq14(range, dy);
	fixed edge = x < 0 ? -ball.x : ((fixed)h->width << 16) - ball.x;

	if (x < -ball.x || x > ((fixed)h->width << 16) - ball.x) range = fix_div(edge, (fixed)dx * 4);
	edge = y < 0 ? -ball.y : ((fixed)h->length << 16) - ball.y;
	if (y < -ball.y || y > ((fixed)h->length << 16) - ball.y) {
		fixed limit = fix_div(edge, (fixed)dy * 4);
		if (limit < range) range = limit;
	}
	p->target.x = ball.x + fix_mulq14(range, dx);
	p->target.y = ball.y + fix_mulq14(range, dy);
}

int play_key(Play *p, uint8_t key, uint32_t now)
{
	if (p->view == PLAY_CARD && p->card == CARD_CLUB) return club_key(p, key);
	if (key == KEY_BACK) return PL_BACK;
	if (p->view == PLAY_CARD) return card_key(p, key);
	/* as on the scene: the result goes on with Select only */
	if (p->onmap) return key == KEY_SELECT || p->cpu || p->state == PS_DONE ? map_click(p, now) : 0;
	if (p->cpu) return hurry(p, now);
	if (key == KEY_CARD && p->round && !practice(p) && (p->state == PS_AIM || p->view == PLAY_MAP)) return open_card(p, CARD_VIEW);
	if (p->view == PLAY_MAP) {
		if (key == KEY_LEFT || key == KEY_RIGHT) {
			p->aim = (angle)(p->aim + (key == KEY_LEFT ? MAP_AIM_STEP : -MAP_AIM_STEP));
			map_point(p);
			p->target_set = 1;
			return PL_MAP;
		}
		if (key != KEY_SELECT && key != KEY_MAP && key != KEY_OTHER) return 0;
		p->view = PLAY_SCENE;
		p->redraw = R_ALL;
		return PL_SCENE;
	}
	if (p->state == PS_DONE) return after_done(p);
	if (key == KEY_SELECT) return click(p, now);
	if (p->state != PS_AIM) return 0;
	switch (key) {
	case KEY_OTHER: case KEY_MAP:
		if (!p->target_range) p->target_range = (fixed)mark_yards(p) << 16;
		if (!p->target_set) map_point(p);
		p->mg.ypp = 0;
		p->view = PLAY_MAP;
		return PL_MAP;
	case KEY_CARD:
		if (practice_green(p)) {
			p->round->repeat ^= 1;
			if (p->round->repeat && p->round->pl[0].taken) {
				p->round->pl[0].taken = 0;
				take(p, 0);
				return PL_RENDER;
			}
			p->redraw |= R_STRIP;
		}
		break;
	case KEY_SHAPE:
		p->shape = ball_shape_next(p->club, p->shape);
		p->redraw |= R_STRIP | R_OVER;
		break;
	case KEY_LEFT:
		p->aim = (angle)(p->aim + AIM_STEP);
		p->target_set = 0;
		p->target_range = 0;
		p->redraw |= R_OVER;
		break;
	case KEY_RIGHT:
		p->aim = (angle)(p->aim - AIM_STEP);
		p->target_set = 0;
		p->target_range = 0;
		p->redraw |= R_OVER;
		break;
	case KEY_UP:
	case KEY_DOWN: {
		/* on the green it's the putter only; to or from the putter changes the view */
		uint8_t c = p->club;
		if (key == KEY_UP && c > CLUB_DR) c--;
		if (key == KEY_DOWN && c < CLUB_PT) c++;
		return choose_club(p, c);
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
	vec2 m = p->target_set ? p->target :
		vec2_make(p->rv.ball.x + fix_mulq14(d, icos(a)), p->rv.ball.y + fix_mulq14(d, isin(a)));
	return render_point(&p->cam, m.x, m.y, hole_height(p->rv.hole, m.x, m.y), x, y, &depth);
}

/* Turns the aim so the marker shows in pixel column tx. */
static void SEC_UI aim_at(Play *p, int16_t tx)
{
	int32_t d = mark_yards(p);
	angle a = (angle)(p->rv.aim - iatan2((int32_t)(tx - p->cam.cx), p->cam.f));
	int16_t i, mx, my;

	p->target_set = 0;
	p->target_range = 0;
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

/* Whether (x, y) is inside the drawn button. */
static int SEC_UI on_box(int16_t x, int16_t y, int16_t bx, int16_t by, int16_t w)
{
	return x >= bx && x < bx + w && y >= by && y < by + ROW_H;
}

static int SEC_MENU map_tap(Play *p, int16_t x, int16_t y, uint32_t now)
{
	const Surface *s = &p->geo;
	int16_t sc = s->scale;
	vec2 point, delta;
	fixed range;

	if (y >= ROW_B && y < ROW_B + ROW_H) {
		if (p->round && !practice(p) && x >= MAP_CARD_X && x < MAP_CARD_X + CARD_W) return open_card(p, CARD_VIEW);
		if (x >= MAP_BACK_X && x < MAP_BACK_X + CARD_W) return play_key(p, KEY_SELECT, now);
	}
	if (p->mg.ypp <= 0 || x < 0 || x >= s->lw || y < 0 || y >= STRIP_Y) return 0;
	point.x = fix_muli(p->mg.ypp, x * sc + sc / 2 - p->mg.left) + p->mg.ypp / 2;
	point.y = p->mg.length - fix_muli(p->mg.ypp, y * sc + sc / 2 - p->mg.top) - p->mg.ypp / 2;
	if (point.x < 0 || point.x >= ((fixed)p->rv.hole->width << 16) || point.y < 0 || point.y >= p->mg.length) return 0;
	delta = vec2_sub(point, p->rv.ball);
	range = vec2_len(delta);
	if (range < FIX_ONE) return 0;
	p->target = point;
	p->target_range = range;
	p->target_set = 1;
	p->aim = vec2_angle(delta);
	return PL_MAP;
}

int play_tap(Play *p, int16_t x, int16_t y, uint32_t now)
{
	const Surface *s = &p->geo;

	if (p->view == PLAY_CARD) {
		int16_t cx = x - (s->lw - 160) / 2, cy = y - (s->lh - 160) / 2;
		int i;
		if (p->card == CARD_CLUB) {
			Surface c = *s;
			gfx_centre(&c, 1);
			return club_tap(p, x - c.ox / c.scale, y - c.oy / c.scale);
		}
		i = p->board ? board_hit(p->round, p->card, cx, cy) : card_hit(p->round, p->card, p->page, cx, cy);
		return i < 0 ? 0 : card_button(p, (uint8_t)i);
	}
	if (p->onmap) return map_click(p, now);
	if (p->cpu) return hurry(p, now);
	if (p->view == PLAY_MAP) return map_tap(p, x, y, now);
	if (p->state == PS_DONE) return after_done(p);
	if (p->state == PS_UP || p->state == PS_DOWN || p->state == PS_RESULT) return click(p, now);
	if (y >= STRIP_Y) {
		if (p->state != PS_AIM) return 0;
		if (on_box(x, y, ACTION_X, ROW_B, ACTION_W)) return click(p, now);
		if (p->lie != LIE_GREEN && on_box(x, y, CLUB_X, ROW_A, CLUB_W)) {
			p->view = PLAY_CARD;
			p->card = CARD_CLUB;
			p->csel = p->club;
			return PL_CARD;
		}
		if (practice_green(p)) {
			if (y >= ROW_B && y < ROW_B + ROW_H) {
				if (x >= REPEAT_X && x < REPEAT_X + REPEAT_W) return play_key(p, KEY_CARD, now);
				if (x >= AIM_MAP_X && x < AIM_MAP_X + MAP_W) return play_key(p, KEY_MAP, now);
			}
		} else {
			if (on_box(x, y, AIM_MAP_X, ROW_B, MAP_W)) return play_key(p, KEY_OTHER, now);
		}
		if (p->club != CLUB_PT && on_box(x, y, SHAPE_X, ROW_A, SHAPE_W)) return play_key(p, KEY_SHAPE, now);
		return 0;
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

/* The same target on the map and over the scene. */
static void SEC_UI draw_target(Play *p, Surface *s, int16_t x, int16_t y)
{
	int16_t u = s->scale, vh = (int16_t)(STRIP_Y * u);
	uint8_t ink = s->depth >= 8 ? UIC_TEXT_HI : UIC_WHITE;
	if (x < 3 * u) x = 3 * u;
	if (y < 3 * u) y = 3 * u;
	if (x > s->w - 4 * u) x = (int16_t)(s->w - 4 * u);
	if (y > vh - 4 * u) y = (int16_t)(vh - 4 * u);
	over(s, x - 3 * u, y - 3 * u, 7 * u, 7 * u, UIC_BLACK);
	over(s, x - 2 * u, y - 2 * u, 5 * u, 5 * u, ink);
	over(s, x - u, y - u, 3 * u, 3 * u, UIC_BLACK);
	over(s, x, y, u, u, ink);
	if (p) patch(p, x - 3 * u, y - 3 * u, 7 * u, 7 * u);
}

/* Aim dots between the ball and the cup. */
static void SEC_UI aim_dots(Play *p, Surface *s)
{
	fixed d = (fixed)mark_yards(p) << 16, depth;
	int16_t k, x, y, u = s->scale;
	for (k = 1; k < 6; k++) {
		fixed t = d / 6 * k;
		vec2 m = vec2_make(p->rv.ball.x + fix_mulq14(t, icos(p->aim)), p->rv.ball.y + fix_mulq14(t, isin(p->aim)));
		if (!render_point(&p->cam, m.x, m.y, hole_height(p->rv.hole, m.x, m.y), &x, &y, &depth)) continue;
		over(s, x, y, 2 * u, 2 * u, s->depth >= 8 ? UIC_BALL_DK : UIC_BLACK);
		over(s, x, y, u, u, UIC_WHITE);
		patch(p, x, y, 2 * u, 2 * u);
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
	if (p->rv.green) {
		uint8_t dark = s->depth >= 8 ? UIC_BALL_DK : UIC_BLACK;
		over(s, sx - 2 * u, sy - u, 5 * u, 3 * u, dark);
		over(s, sx - u, sy - 2 * u, 3 * u, 5 * u, dark);
		over(s, sx - u, sy - u, 3 * u, 3 * u, UIC_WHITE);
		patch(p, sx - 2 * u, sy - 2 * u, 5 * u, 5 * u);
		return;
	}
	n = depth < (50L << 16) ? 2 * u : u;
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

/* Up is along the shot in the scene and along the hole's y axis on the map. */
static void SEC_UI rose(const Play *p, Surface *s, uint8_t map)
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
	/* The axis at the top of the view. */
	ui_fill(s, x + 12, y + 1, 1, 1, UIC_PAPER);
	if (p->wind_mph) {
		angle up = map ? 0x4000 : p->aim;
		uint8_t d = (uint8_t)((((uint16_t)(p->wind_dir - up) + 4096) >> 13) & 7);
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
		to_par(v, &n, rules_tour_to_par(r, p->cur));
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

/* The club and its usual carry stay in the same well through a shot. */
static void SEC_UI panel_club(Surface *s, const Play *p, uint8_t active)
{
	char t[12];
	int16_t n = 0;

	add(t, &n, p->club == CLUB_PT ? "Putter" : ball_club_name(p->club));
	if (p->club != CLUB_PT) {
		add(t, &n, " ");
		play_num(t + n, mark_yards(p));
	}
	if (active && p->lie != LIE_GREEN) button(s, CLUB_X, ROW_A, CLUB_W, t);
	else readout(s, CLUB_X, ROW_A, CLUB_W, t, -1, UIC_PAPER);
}

static void SEC_UI shot_identity(Surface *s, const Play *p)
{
	char t[24];
	int16_t n = 0;

	if (practice(p)) {
		add(t, &n, "Ball ");
		play_num(t + n, (uint32_t)p->round->balls + 1);
	} else {
		add(t, &n, p->round ? p->round->pl[p->cur].name : "Player 1");
		add(t, &n, ", shot ");
		play_num(t + n, p->stroke);
	}
	readout(s, LIE_X, ROW_B, (p->club == CLUB_PT ? s->lw - 2 : ROSE_X - 1) - LIE_X, t, -1, UIC_PAPER);
}

static const char * SEC_MENU result_action(const Play *p)
{
	const Round *r = p->round;
	if (p->cpu) return 0;
	if (p->state == PS_RESULT) {
		if (p->result_pending) return 0;
		if (practice(p)) {
			return r->mode == MODE_RANGE || r->repeat || p->shot.result == SHOT_HOLED ||
			       given(p, p->shot.stop, p->shot.lie) ? "Next ball" : "Next shot";
		}
		if (p->shot.result == SHOT_HOLED || given(p, p->shot.stop, p->shot.lie)) return 0;
		return "Next shot";
	}
	if (p->state != PS_DONE) return 0;
	if (!r) return "Done";
	if (!rules_hole_over(r)) return "Next turn";
	if (r->hole + 1 == r->holes) return r->mode == MODE_TOURN ? "Leaders" : "Scorecard";
	if (r->mode == MODE_MATCH || r->mode == MODE_BEST) {
		uint8_t left;
		int8_t up = rules_match(r, &left);
		if (up > (int8_t)left || -up > (int8_t)left) return "Scorecard";
	}
	return "Next hole";
}

static void SEC_UI result_dirty(Play *p, const Surface *s)
{
	int16_t extra = p->cpu ? 0 : ROW_H;
	dirty(p, 0, (int16_t)((STRIP_Y - extra) * s->scale), s->w,
	      (int16_t)((RENDER_STATUS + extra) * s->scale));
}

/* Keep the club in view when a result fits beside it. */
static void SEC_UI shot_result(Surface *s, const Play *p)
{
	const char *action = result_action(p);
	const char *top = p->msg2, *bottom = p->msg1;
	int16_t n = 0, w = (int16_t)(s->lw - 2 - SHAPE_X), room = (int16_t)((w - 3) / UI_CHAR);

	if (action) button(s, s->lw - 2 - BOX_W(9), STRIP_Y - ROW_H, BOX_W(9), action);
	while (top[n]) n++;
	if (n > room) {
		top = p->msg1;
		bottom = p->msg2;
		for (n = 0; top[n]; n++) {}
	}
	if (n <= room) {
		panel_club(s, p, 0);
		readout(s, SHAPE_X, ROW_A, w, top, -1, p->state == PS_DONE ? UIC_TEXT_HI : UIC_PAPER);
		readout(s, 2, ROW_B, s->lw - 4, bottom, -1, p->state == PS_DONE ? UIC_PAPER : UIC_TEXT_HI);
		return;
	}
	/* Long results such as stroke and distance need the whole width. */
	well(s, 2, ROW_A, s->lw - 4, 2 * ROW_H + 1);
	box_text(s, 2, ROW_A, s->lw - 4, p->msg1, -1, 0, UIC_TEXT_HI);
	box_text(s, 2, ROW_B, s->lw - 4, p->msg2, -1, 0, UIC_PAPER);
}

static void SEC_UI address_controls(Surface *s, const Play *p)
{
	static const char * const short_lie[] = {
		"Tee", "Fwy", "Cut", "Rgh", "D/R", "Snd", "Plg", "Frg", "Grn", "Pth"
	};
	char t[24];
	int16_t w = s->lw >= 200 ? LIE_W : BOX_W(3);
	int16_t who_x = AIM_LIE_X + w, n, x;
	const char *lie = w == LIE_W ? ball_lie_name(p->lie) : short_lie[p->lie];

	button(s, ACTION_X, ROW_B, ACTION_W, p->club == CLUB_PT ? "Putt" : "Swing");
	if (practice_green(p)) {
		x = REPEAT_X;
		w = x - 1 - AIM_LIE_X;
		if (w >= BOX_W(3)) readout(s, AIM_LIE_X, ROW_B, w, lie, -1, UIC_PAPER);
		button(s, x, ROW_B, REPEAT_W, "");
		box_text(s, x, ROW_B, BOX_W(6), "Repeat", 6, -1, UIC_INK);
		x += BOX_W(6);
		ui_fill(s, x, ROW_B + 3, 5, 5, UIC_INK);
		ui_fill(s, x + 1, ROW_B + 4, 3, 3, UIC_WHITE);
		if (p->round->repeat) {
			ui_fill(s, x + 1, ROW_B + 5, 1, 1, UIC_INK);
			ui_fill(s, x + 2, ROW_B + 6, 1, 1, UIC_INK);
			ui_fill(s, x + 3, ROW_B + 4, 1, 2, UIC_INK);
		}
		button(s, AIM_MAP_X, ROW_B, MAP_W, "Map");
		return;
	}
	readout(s, AIM_LIE_X, ROW_B, w, lie, -1, UIC_PAPER);
	n = AIM_MAP_X - 1 - who_x;
	if (n > BOX_W(8)) n = BOX_W(8);
	standing(p, t, (n - 3) / UI_CHAR);
	readout(s, who_x, ROW_B, n, t, 0, UIC_PAPER);
	button(s, AIM_MAP_X, ROW_B, MAP_W, "Map");
}

static void SEC_UI draw_strip(Play *p, Surface *s)
{
	char t[24];
	int16_t n = 0;
	uint8_t swing = p->state == PS_UP || p->state == PS_DOWN;
	uint8_t address = p->state == PS_AIM && !p->cpu;

	face(s);
	if (p->state == PS_RESULT || p->state == PS_DONE) {
		shot_result(s, p);
		result_dirty(p, s);
		return;
	} else {
		panel_club(s, p, address);
		if (p->club == CLUB_PT) {
			add(t, &n, "Cup ");
			n += play_num(t + n, pin_feet(p));
			add(t, &n, " ft");
			readout(s, SHAPE_X, ROW_A, s->lw - 2 - SHAPE_X, t, 1, UIC_TEXT_HI);
		} else {
			int16_t x = PIN_X;
			if (address) button(s, SHAPE_X, ROW_A, SHAPE_W, ball_shape_name(p->shape));
			else if (p->shape) readout(s, SHAPE_X, ROW_A, SHAPE_W, ball_shape_name(p->shape), 0, UIC_PAPER);
			else {
				x = SHAPE_X;
				add(t, &n, "Pin ");
			}
			n += play_num(t + n, pin_yards(p));
			add(t, &n, "y");
			readout(s, x, ROW_A, PIN_X + PIN_W - x, t, 1, UIC_TEXT_HI);
		}
		if (!swing) {
			if (p->club != CLUB_PT) rose(p, s, 0);
			if (address) address_controls(s, p);
			else shot_identity(s, p);
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

/* The meter in a well on the panel's lower row, columns x0 to x1 only: the
 * track, the power so far in brass, quarter marks, the snap line and the
 * marker (black between white sides, so it shows on the track and the
 * power). The well's edge only on a full draw (x0 0); a frame's redraw
 * keeps to the rows inside and draws each pixel about once, to stay cheap
 * on the IIIx. */
static void SEC_UI draw_meter(Play *p, Surface *s, int16_t x0, int16_t x1)
{
	int16_t y = ROW_B, top = p->state == PS_UP ? p->mpos : p->turn, k, m = MTR_X + (p->mpos >> 4);
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
		k = (int16_t)(sx + p->putt_tick);
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
		if (p->round->repeat || p->round->mode == MODE_RANGE || sh->result == SHOT_HOLED ||
		    given(p, p->rv.ball, p->lie) || p->stroke > 9) {
			if (!p->round->repeat) p->round->balls++;
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
		if (given(p, p->rv.ball, p->lie)) done(p, p->stroke, 1);
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

int play_next_view(const Play *p, RenderView *v)
{
	const Shot *sh = &p->shot;
	const Hole *h = p->rv.hole;
	vec2 ball = sh->stop, to = h->pins[p->rv.pin & 3];
	uint8_t lie = sh->lie, stroke, player = p->cur;
	uint16_t yd;

	/* as apply and next would have it, with nothing changed */
	if ((p->state != PS_FLY && p->state != PS_RESULT) || p->applied || !p->have_shot || (p->round && MODE_PRACTICE(p->round->mode)) || sh->result == SHOT_HOLED) return 0;
	stroke = (uint8_t)(p->stroke + (sh->result == SHOT_OK ? 1 : 2));
	if (given(p, ball, lie)) return 0;
	if (p->cpu && stroke > h->par + 4) return 0;
	if (p->round) {
		Round r = *p->round;
		RoundPlayer *rp = &r.pl[p->cur];
		int n;
		rp->ball = ball;
		rp->lie = lie;
		rp->taken = (uint8_t)(stroke - 1);
		n = rules_next(&r, to);
		if (n >= 0 && (n != p->cur || p->cpu)) {
			/* another player's turn: only a person's, whose aim is known */
			rp = &r.pl[n];
			if (rp->cpu || r.mode == MODE_GREEN) return 0;
			player = (uint8_t)n;
			if (rp->taken) {
				ball = rp->ball;
				lie = rp->lie;
			} else {
				ball = h->tees[h->tee].pos;
				lie = LIE_TEE;
			}
		}
	}
	/* as setup: aimed at the pin, or the hole's aim point from the tee; the putter has the green view */
	yd = (uint16_t)((vec2_len(vec2_sub(to, ball)) + FIX_HALF) >> 16);
	if (ball.x == h->tees[h->tee].pos.x && ball.y == h->tees[h->tee].pos.y) to = h->aim;
	*v = p->rv;
	v->ball = ball;
	v->aim = vec2_angle(vec2_sub(to, ball));
	v->green = lie == LIE_GREEN || lie == LIE_FRINGE || ball_pick(yd, lie) == CLUB_PT;
	v->player = player;
	return 1;
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
	/* a shot's map half drawn at the old size starts again */
	if (!p->onmap && p->mprep < 3) p->mprep = 0;
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
 * finish once the ball is struck. With the putter he stays at address.
 * During the swing only s gets the new pose unless an overlay is over him,
 * to keep the meter's frames short: the kept copy catches up with the
 * finish, which every swing ends with. */
static void SEC_UI pose(Play *p, Surface *s, Surface *bg)
{
	RenderGolfer *g = p->golf;
	uint8_t want, ball = p->state <= PS_DOWN, k, over = 0;
	int16_t x, y, w, h;

	if (!g || !g->w || p->rv.green) return;
	/* the meter's first frame shows on its own, he swings back on the next */
	if (p->state == PS_UP && p->mfirst == 2 && (p->redraw & R_STRIP)) return;
	if (p->club == CLUB_PT || p->state == PS_AIM) want = POSE_ADDRESS;
	else if (p->state == PS_UP) want = p->mpos < (S16 + W16) / 2 ? POSE_BACK : POSE_TOP;
	else if (p->state == PS_DOWN) want = POSE_TOP;
	else want = POSE_FINISH;
	if (want == g->shown && ball == g->ball) return;
	if (render_pose(s, g, want, ball, &x, &y, &w, &h) < 0) return;
	/* overlays over him come off the kept copy and go on again */
	for (k = 0; k < p->npatch; k++) {
		const URect *r = &p->patch[k];
		if (r->x < x + w && x < r->x + r->w && r->y < y + h && y < r->y + r->h) over = 1;
	}
	if (bg && (over || p->state > PS_DOWN || want == POSE_ADDRESS)) render_pose(bg, g, want, ball, &x, &y, &w, &h);
	if (over) p->redraw |= R_OVER;
	g->shown = want;
	g->ball = ball;
	if (p->gdirty.w > 0) dirty(p, p->gdirty.x, p->gdirty.y, p->gdirty.w, p->gdirty.h);
	p->gdirty.x = x;
	p->gdirty.y = y;
	p->gdirty.w = w;
	p->gdirty.h = h;
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

/* The path point the flight has got to at now: FLY_MS a point in the air,
 * ROLL_MS on the ground. The first call starts the clock. */
static int16_t SEC_UI fly_point(Play *p, uint32_t now)
{
	int32_t e, k, fly = (int32_t)p->shot.fly_n * FLY_MS;
	if (p->shown == -2) {
		p->tf = now;
		p->shown = -1;
	}
	e = (int32_t)(now - p->tf);
	k = e < fly ? e / FLY_MS : p->shot.fly_n + (e - fly) / ROLL_MS;
	if (k > p->shot.n - 1) k = p->shot.n - 1;
	return (int16_t)k;
}

/* The shot on the map: the map framed on the shot, the trail drawn as the
 * ball goes, the ball over it (the pixels under it kept in ubits), and the
 * panel with the carry or total so far, then the result. */

#define MAP_PAD   (12L << 16)   /* yards shown round the shot */
#define MAP_MIN_W (60L << 16)   /* the map shows at least this much across */
#define MAP_MIN_H (80L << 16)   /* and along */

static void SEC_UI box_add(MapBox *b, fixed x, fixed y)
{
	if (x < b->x0) b->x0 = x;
	if (x > b->x1) b->x1 = x;
	if (y < b->y0) b->y0 = y;
	if (y > b->y1) b->y1 = y;
}

/* Pads b and grows it round its middle to the least size. */
static void SEC_UI box_pad(MapBox *b)
{
	fixed d;
	b->x0 -= MAP_PAD; b->x1 += MAP_PAD;
	b->y0 -= MAP_PAD; b->y1 += MAP_PAD;
	if ((d = MAP_MIN_W - (b->x1 - b->x0)) > 0) { b->x0 -= d / 2; b->x1 += d / 2; }
	if ((d = MAP_MIN_H - (b->y1 - b->y0)) > 0) { b->y0 -= d / 2; b->y1 += d / 2; }
}

/* Yards a pixel that b needs on s. */
static fixed SEC_UI box_ypp(const MapBox *b, const Surface *s)
{
	fixed a = (b->x1 - b->x0) / s->w, c = (b->y1 - b->y0) / (int16_t)((s->lh - RENDER_STATUS) * s->scale);
	return a > c ? a : c;
}

/* The part of the hole the map shows for the shot: where it started and
 * every point of its path, and the pin as well if that doesn't make the
 * map more than half as small again. */
static void SEC_UI shot_box(const Play *p, const Surface *s, MapBox *b)
{
	MapBox a;
	vec2 pn = pin(p);
	int16_t k;

	b->x0 = b->x1 = p->shot.start.x;
	b->y0 = b->y1 = p->shot.start.y;
	for (k = 0; k < p->shot.n; k++) box_add(b, (fixed)p->shot.path[k].x * 4096, (fixed)p->shot.path[k].y * 4096);
	a = *b;
	box_add(&a, pn.x, pn.y);
	box_pad(b);
	box_pad(&a);
	if (box_ypp(&a, s) <= box_ypp(b, s) + (box_ypp(b, s) >> 1)) *b = a;
}

/* Path point k on the map, in pixels. */
static void SEC_UI map_xy(const Play *p, int16_t k, int16_t *x, int16_t *y)
{
	const BallPt *b = &p->shot.path[k];
	*x = (int16_t)(p->mg.left + ((fixed)b->x * 4096) / p->mg.ypp);
	*y = (int16_t)(p->mg.top + (p->mg.length - (fixed)b->y * 4096) / p->mg.ypp);
}

/* A fill over the map's view that goes to the screen. */
static void SEC_UI mput(Play *p, Surface *s, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c)
{
	over(s, x, y, w, h, c);
	dirty(p, x, y, w, h);
}

/* The trail from path point a to b: a bright line with a black edge,
 * yellow in the air and white on the ground in colour, white on greys.
 * The edges go down first, then the line, so the line stays whole. */
static void SEC_UI trail(Play *p, Surface *s, int16_t a, int16_t b)
{
	int16_t k, u = s->scale;
	for (k = a + 1; k <= b; k++) {
		int16_t x0, y0, x1, y1, dx, dy, n, j, pass;
		uint8_t c = k <= p->shot.fly_n ? UIC_TEXT_HI : UIC_WHITE;
		map_xy(p, (int16_t)(k - 1), &x0, &y0);
		map_xy(p, k, &x1, &y1);
		dx = x1 - x0;
		dy = y1 - y0;
		n = dx < 0 ? -dx : dx;
		if (dy > n || -dy > n) n = dy < 0 ? -dy : dy;
		if (n > 400) continue;
		for (pass = 0; pass < 2; pass++)
			for (j = pass || k == 1 ? 0 : 1; j <= n; j++) {
				int16_t x = x0 + (n ? (int16_t)((int32_t)dx * j / n) : 0), y = y0 + (n ? (int16_t)((int32_t)dy * j / n) : 0);
				if (pass) mput(p, s, x, y, u, u, c);
				else mput(p, s, x - u, y - u, 3 * u, 3 * u, UIC_BLACK);
			}
	}
}

/* Keeps (put 0) or puts back (put 1) the pixels under the ball. */
static void SEC_UI under_io(Play *p, Surface *s, uint8_t put)
{
	URect *r = &p->under;
	int16_t j, i, b0, n, k = 0;
	if (r->w <= 0) return;
	b0 = (int16_t)(((uint16_t)r->x << s->lg) >> 3);
	n = (int16_t)((((uint16_t)(r->x + r->w - 1) << s->lg) >> 3) - b0 + 1);
	if (n * r->h > PLAY_UNDER) {
		r->w = 0;
		return;
	}
	for (j = 0; j < r->h; j++) {
		uint8_t *q = s->bits + MULU16(r->y + j, s->rowBytes) + b0;
		for (i = 0; i < n; i++, k++) {
			if (put) q[i] = p->ubits[k];
			else p->ubits[k] = q[i];
		}
	}
	if (put) dirty(p, r->x, r->y, r->w, r->h);
}

/* The trail on to the point shown, the landing mark once it's down, and
 * the ball there: white with a black edge, bigger than the trail. */
static void SEC_UI map_ball(Play *p, Surface *s)
{
	int16_t k = p->shown, u = s->scale, x, y, x1, y1, vh = (int16_t)((s->lh - RENDER_STATUS) * u);
	const Shot *sh = &p->shot;

	under_io(p, s, 1);
	p->under.w = 0;
	if (p->mg.ypp <= 0) return;
	if (k > p->mdrawn) {
		trail(p, s, p->mdrawn < 0 ? 0 : p->mdrawn, k);
		if (sh->fly_n > 0 && sh->fly_n < sh->n && k >= sh->fly_n) {
			/* where it came down: a black ring, over the roll's trail */
			map_xy(p, sh->fly_n, &x, &y);
			mput(p, s, x - 3 * u, y - 3 * u, 7 * u, u, UIC_BLACK);
			mput(p, s, x - 3 * u, y + 3 * u, 7 * u, u, UIC_BLACK);
			mput(p, s, x - 3 * u, y - 2 * u, u, 5 * u, UIC_BLACK);
			mput(p, s, x + 3 * u, y - 2 * u, u, 5 * u, UIC_BLACK);
		}
		p->mdrawn = k;
	}
	if (k < 0 || (sh->result == SHOT_HOLED && k == sh->n - 1)) return;
	map_xy(p, k, &x, &y);
	x -= 2 * u;
	y -= 2 * u;
	x1 = x + 5 * u;
	y1 = y + 5 * u;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x1 > s->w) x1 = s->w;
	if (y1 > vh) y1 = vh;
	if (x1 <= x || y1 <= y) return;
	p->under.x = x;
	p->under.y = y;
	p->under.w = x1 - x;
	p->under.h = y1 - y;
	under_io(p, s, 0);
	if (p->under.w <= 0) return;
	map_xy(p, k, &x, &y);
	mput(p, s, x - 2 * u, y - 2 * u, 5 * u, 5 * u, UIC_BLACK);
	mput(p, s, x - u, y - u, 3 * u, 3 * u, UIC_WHITE);
}

/* The panel's live readout: the carry so far in the air, the total on the
 * ground. Drawn only when it changes. */
static void SEC_UI map_live(Play *p, Surface *s, uint8_t force)
{
	char t[12];
	int16_t n = 0, k = p->shown < 0 ? 0 : p->shown;
	const BallPt *b = &p->shot.path[k];
	uint8_t roll = k >= p->shot.fly_n;
	fixed d = vec2_len(vec2_make((fixed)b->x * 4096 - p->shot.start.x, (fixed)b->y * 4096 - p->shot.start.y));
	uint16_t v = (uint16_t)((d + FIX_HALF) >> 16);

	if (v > 999) v = 999;
	v |= roll ? 0x8000 : 0;
	if (!force && v == p->mlive) return;
	p->mlive = v;
	add(t, &n, roll ? "Total " : "Carry ");
	play_num(t + n, v & 0x7FFF);
	readout(s, SHAPE_X, ROW_A, PIN_X + PIN_W - SHAPE_X, t, -1, UIC_TEXT_HI);
	dirty(p, (int16_t)(SHAPE_X * s->scale), (int16_t)(ROW_A * s->scale), (int16_t)((PIN_X + PIN_W - SHAPE_X) * s->scale),
	      (int16_t)(ROW_H * s->scale));
}

/* The panel for a shot on the map: in flight the club, the live readout,
 * the wind and who's playing; then the result or the score. */
static void SEC_UI fly_info(Surface *s, Play *p);
static void SEC_UI fly_panel(Surface *s, Play *p)
{
	face(s);
	fly_info(s, p);
}

/* fly_panel's words, over its face. */
static void SEC_UI fly_info(Surface *s, Play *p)
{
	if (p->state != PS_FLY) {
		shot_result(s, p);
		return;
	}
	panel_club(s, p, 0);
	map_live(p, s, 1);
	rose(p, s, 1);
	shot_identity(s, p);
}

/* The map for the shot, rows y0 to y1 of it, and at the end the start
 * mark, with no trail or ball yet. Returns 1 when it's all drawn. */
static int SEC_UI fly_map_rows(Play *p, Surface *s, int16_t y0, int16_t y1)
{
	MapBox b;
	int16_t x, y, u = s->scale;
	int k;

	shot_box(p, s, &b);
	k = render_map_rows(s, &p->rv, &b, &p->mg, y0, y1);
	if (k <= 0) return k;
	p->under.w = 0;
	p->mdrawn = -1;
	p->mlive = 0xFFFF;
	/* the start: a black square with a white ring */
	x = (int16_t)(p->mg.left + p->shot.start.x / p->mg.ypp);
	y = (int16_t)(p->mg.top + (p->mg.length - p->shot.start.y) / p->mg.ypp);
	over(s, x - 2 * u, y - 2 * u, 5 * u, 5 * u, UIC_BLACK);
	over(s, x - u, y - u, 3 * u, 3 * u, UIC_WHITE);
	over(s, x, y, u, u, UIC_BLACK);
	return 1;
}

/* Draws the map for the shot on it, with the trail so far and the ball. */
static void SEC_UI fly_map_draw(Play *p, Surface *s)
{
	if (fly_map_rows(p, s, 0, 0x7FFF) <= 0) return;
	p->dirty.w = 0;
	if (p->shown >= 0) map_ball(p, s);
}

int play_map_wanted(const Play *p)
{
	return p->mapview && p->state == PS_FLY && p->view == PLAY_SCENE && !p->onmap && p->mprep < 3 && p->shown < p->shot.fly_n &&
	       p->club != CLUB_PT && (int32_t)p->shot.fly_n * FLY_MS >= PLAY_MAP_MIN_MS;
}

int play_map_prep(Surface *m, Play *p, int16_t rows)
{
	int k;
	URect d = p->dirty;    /* the scene's, not touched here */
	if (!play_map_wanted(p)) return p->mprep == 2;
	if (p->mprep == 2) {
		/* ready: the trail keeps up with the ball, so the switch has little to draw */
		if (p->shown > p->mdrawn) map_ball(p, m);
		p->dirty = d;
		return 1;
	}
	if (!p->mprep) {
		p->mprep = 1;
		p->mrow = 0;
	}
	if (p->mrow < 0) {
		/* the panel's face, its words, then the trail so far, each a call of its own */
		if (p->mrow == -1) {
			face(m);
			p->mrow = -2;
			k = 0;
		} else if (p->mrow == -2) {
			fly_info(m, p);
			p->mrow = -3;
			k = 0;
		} else {
			if (p->shown > p->mdrawn) map_ball(p, m);
			p->mprep = 2;
			k = 1;
		}
		p->dirty = d;
		return k;
	}
	if (rows < 1) rows = 1;
	k = fly_map_rows(p, m, p->mrow, (int16_t)(p->mrow + rows));
	if (k < 0) {
		p->mprep = 3;
		return 0;
	}
	p->mrow = k ? -1 : (int16_t)(p->mrow + rows);
	p->dirty = d;
	return 0;
}

/* A frame of the shot on the map: the ball on, then the result, then the
 * next shot or the score, as play_frame does in the scene. */
static int SEC_UI fly_map(Play *p, Surface *s, uint32_t now)
{
	int f;

	p->dirty.w = 0;
	if (p->state == PS_FLY) {
		int16_t i = fly_point(p, now);
		landed(p, i);
		if (i != p->shown) {
			p->shown = i;
			map_ball(p, s);
		}
		if (i >= p->shot.n - 1) {
			stopped(p);
			result(p);
			p->state = PS_RESULT;
			p->t0 = now;
			fly_panel(s, p);
			p->redraw &= ~R_STRIP;
			result_dirty(p, s);
		} else {
			map_live(p, s, 0);
		}
		return PL_ANIM | (p->dirty.w > 0 ? PL_DIRTY : 0);
	}
	if (p->state == PS_RESULT) {
		if ((int32_t)(now - p->t0) < (p->cpu ? PLAY_CPU_MS : RESULT_MS)) {
			if (!(p->redraw & R_STRIP)) return PL_ANIM;
			fly_panel(s, p);
			p->redraw &= ~R_STRIP;
			result_dirty(p, s);
			return PL_ANIM | PL_DIRTY;
		}
		f = next(p);
		if (f) return f;
		/* holed: the score on the panel */
		fly_panel(s, p);
		result_dirty(p, s);
		return PL_DIRTY | (p->cpu ? PL_ANIM : 0);
	}
	if (p->state == PS_DONE && p->cpu) return waited(p, now, PLAY_CPU_MS) ? after_done(p) : PL_ANIM;
	return 0;
}

void play_result_pending(Play *p, uint8_t pending)
{
	if (p->state == PS_FLY) p->result_pending = pending;
	else if (p->state == PS_RESULT && p->result_pending && !pending) {
		p->result_pending = 0;
		p->redraw |= R_STRIP;
	}
}

int play_frame(Play *p, Surface *s, Surface *bg, uint32_t now)
{
	int flags = 0;
	uint8_t k;

	if (p->view == PLAY_MAP) return p->onmap ? fly_map(p, s, now) : map_frame(p, now);
	if (p->view != PLAY_SCENE) return 0;
	camera(p, s);
	p->dirty.w = 0;
	p->gdirty.w = 0;
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
			if (p->club == CLUB_PT) {
				launch(p);
			} else {
				p->state = PS_DOWN;
				p->autoturn = 1;
				p->tg = now;
			}
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
		int16_t i = fly_point(p, now);
		landed(p, i);
		if (p->mprep && p->mprep < 3 && i >= p->shot.fly_n) p->mprep = 3;    /* down before the map was ready */
		if (p->mprep == 2 && (int32_t)(now - p->tf) >= PLAY_MAP_AFTER_MS) {
			/* over to the map for the rest of the flight: the first map frame draws the trail so far */
			p->shown = -1;
			p->onmap = 1;
			p->view = PLAY_MAP;
			return PL_MAP | PL_ANIM;
		}
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
			if (p->state == PS_AIM && p->rv.aim != p->aim &&
			    (!ok || mx < p->cam.cx - AIM_EDGE * s->scale || mx > p->cam.cx + AIM_EDGE * s->scale)) {
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
		if (ok && p->target_set) draw_target(p, s, mx, my);
		else if (ok && !p->rv.green) draw_mark(p, s, mx, my);
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
	} else if ((p->redraw & R_METER) && (p->state == PS_UP || p->state == PS_DOWN)) {
		/* only between where the marker was and where it is, as the power fill follows it */
		int16_t m = MTR_X + (p->mpos >> 4), a = m < p->mshown ? m : p->mshown, b = m < p->mshown ? p->mshown : m;
		draw_meter(p, s, a - 3, b + 3);
	}
	p->redraw = 0;
	if (p->dirty.w > 0 || p->gdirty.w > 0) flags |= PL_DIRTY;
	return flags;
}

void play_shown(Play *p, uint32_t now)
{
	if (p->autoturn == 1) {
		/* the full marker is on the screen: the grace for a late power press runs from now */
		p->tg = now;
		p->autoturn = 2;
	}
	if (!p->mfirst) return;
	if (p->state == PS_UP && p->view == PLAY_SCENE) {
		/* the clock as if the marker on the screen had been reached just now: it never steps back
		 * (the time is rounded up so meter_pos gives at least mpos) */
		int32_t d = p->mpos > S16 ? p->mpos - S16 : 0;
		p->t0 = now - (uint32_t)((d * MTR_UP_MS + (W16 - S16) - 1) / (W16 - S16));
		p->mfirst--;
	} else {
		p->mfirst = 0;
	}
}

int gate_press(KeyGate *g, uint16_t code, uint16_t key, uint8_t repeat, uint8_t up, uint32_t now)
{
	uint32_t e = now - g->t;

	if (repeat) return 0;
	if (g->code) {
		if (code != g->code && e < GATE_PAIR_MS) return 0;
		if (code == g->code && (e < GATE_BOUNCE_MS || (g->held && g->ups && e < GATE_HELD_MS))) return 0;
	}
	g->code = code;
	g->key = key;
	g->held = up;
	g->t = now;
	return 1;
}

void gate_release(KeyGate *g, uint16_t code, uint16_t key)
{
	if (!g->code || (code != g->code && (!key || key != g->key))) return;
	g->held = 0;
	g->ups = 1;
}

void gate_flush(KeyGate *g)
{
	g->held = 0;
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
	n = play_num(t, h->tees[h->tee].yards);
	add(t, &n, " yd");
	readout(s, 4 + BOX_W(7) + BOX_W(5), ROW_A, BOX_W(6), t, 1, UIC_TEXT_HI);
}

void play_status(Surface *s, const Play *p, const char *info)
{
	int16_t w = s->lw - 4;

	if (p->onmap) {
		/* const only to the callers: the live readout notes what it showed */
		fly_panel(s, (Play *)p);
		return;
	}
	if (p->cpu && p->state == PS_RESULT) {
		/* a computer golfer's shot: who and which shot, and where it went */
		char t[24];
		int16_t n = 0;
		face(s);
		add(t, &n, p->round->pl[p->cur].name);
		add(t, &n, " #");
		play_num(t + n, p->stroke);
		panel_club(s, p, 0);
		readout(s, SHAPE_X, ROW_A, s->lw - 2 - SHAPE_X, t, -1, UIC_PAPER);
		readout(s, 2, ROW_B, s->lw - 4, p->msg1, -1, UIC_TEXT_HI);
		return;
	}
	if (p->cpu && p->state == PS_DONE) {
		face(s);
		shot_result(s, p);
		return;
	}
	if (!p->cpu && p->view == PLAY_MAP && p->state == PS_AIM) {
		char t[14];
		int16_t n = 0;
		face(s);
		panel_club(s, p, 0);
		add(t, &n, "Aim ");
		n += play_num(t + n, (uint32_t)((vec2_len(vec2_sub(p->target, p->rv.ball)) + FIX_HALF) >> 16));
		add(t, &n, "y");
		readout(s, SHAPE_X, ROW_A, ROSE_X - SHAPE_X - 1, t, -1, UIC_TEXT_HI);
		n = 0;
		add(t, &n, "Hole ");
		play_num(t + n, p->rv.hole->number);
		readout(s, 2, ROW_B, MAP_BACK_X - 3, t, -1, UIC_PAPER);
		button(s, MAP_BACK_X, ROW_B, CARD_W, "Back");
		if (p->round && !practice(p)) button(s, MAP_CARD_X, ROW_B, CARD_W, "Card");
		rose(p, s, 1);
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

static void SEC_MENU draw_clubs(Surface *s, const Play *p)
{
	static const UiDialog d = { "Club", "", "", 0, 0, { 4, 15, 152, 114 }, { { 0 } } };
	uint8_t i;

	ui_dialog_draw(s, &d);
	for (i = 0; i <= CLUB_COUNT; i++) {
		int16_t x = CLUB_PICK_X + (i % CLUB_PICK_COLS) * CLUB_PICK_W;
		int16_t y = CLUB_PICK_Y + (i / CLUB_PICK_COLS) * CLUB_PICK_H;
		char t[12];
		int16_t n = 0;
		if (i == CLUB_COUNT) add(t, &n, "Back");
		else if (i == CLUB_PT) add(t, &n, "Putter");
		else {
			add(t, &n, ball_club_name(i));
			add(t, &n, " ");
			play_num(t + n, ball_shape_carry(i, p->lie,
			         ball_shape_ok(i, p->shape) ? p->shape : SHAPE_NORMAL));
		}
		if (p->csel == i) readout(s, x, y, CLUB_PICK_W - 1, t, 0, UIC_TEXT_HI);
		else button(s, x, y, CLUB_PICK_W - 1, t);
	}
}

void play_card(Surface *s, Play *p)
{
	Surface c = *s;
	p->geo = *s;
	if (p->card == CARD_CLUB) {
		draw_strip(p, s);
		gfx_centre(&c, 1);
		draw_clubs(&c, p);
		return;
	}
	/* the panel with the hole under it, and the card in a 160 unit square in the middle */
	if (p->rv.hole) {
		hole_row(s, p->rv.hole);
		readout(s, 2, ROW_B, s->lw - 4, p->rv.hole->name, -1, UIC_PAPER);
	}
	gfx_centre(&c, 1);
	if (p->round && p->board) board_draw(&c, p->round, p->card, p->csel);
	else if (p->round) card_draw(&c, p->round, p->card, p->page, p->csel);
}

void play_map(Surface *s, Play *p)
{
	RenderMap m;
	char t[12];
	int16_t n, x, y, sc = s->scale, k;
	uint8_t aiming = p->view == PLAY_MAP && p->state == PS_AIM && !p->cpu;

	p->geo = *s;
	if (p->onmap) {
		fly_map_draw(p, s);
		return;
	}
	if (render_target_map(s, &p->rv, &m, aiming ? &p->target : 0) != 0) return;
	/* the last shot's path, dotted */
	if (p->have_shot) {
		int16_t last_x = -3000, last_y = -3000;
		for (k = 0; k < p->shot.n; k += 2) {
			const BallPt *b = &p->shot.path[k];
			x = (int16_t)(m.left + ((fixed)b->x * 4096) / m.ypp);
			y = (int16_t)(m.top + (m.length - ((fixed)b->y * 4096)) / m.ypp);
			if (aiming) {
				int16_t dx = x - last_x, dy = y - last_y, gap = 5 * sc;
				if (dx > -gap && dx < gap && dy > -gap && dy < gap) continue;
				if (x >= m.bx - sc && x <= m.bx + sc && y >= m.by - sc && y <= m.by + sc) continue;
				over(s, x - sc, y - sc, 3 * sc, 3 * sc, UIC_BLACK);
				over(s, x, y, sc, sc, UIC_WHITE);
				last_x = x;
				last_y = y;
			} else if (y < (STRIP_Y - 1) * sc) {
				gfx_fill(s, x, y, sc, sc, UIC_WHITE);
			}
		}
	}
	if (aiming) {
		p->mg = m;
		x = (int16_t)(m.left + p->target.x / m.ypp);
		y = (int16_t)(m.top + (m.length - p->target.y) / m.ypp);
		draw_target(0, s, x, y);
		return;
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
