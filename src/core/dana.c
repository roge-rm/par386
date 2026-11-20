#include "dana.h"
#include "trig.h"

#define RAIL_IN (DANA_RAIL_W - 4)
#define BOX_H 13
#define MAP_Y 30
#define MAP_H 96
#define WIND_Y 127
#define BOOK_Y 143
#define PIN_Y 17
#define CLUB_Y 32
#define LIE_Y 47
#define SHAPE_Y 62
#define NAV_Y 125
#define ACT_Y 143

static const char dirs[8][3] = { "N", "NW", "W", "SW", "S", "SE", "E", "NE" };

static int16_t SEC_DANA append(char *to, const char *from)
{
	int16_t n = 0;
	while (from[n]) { to[n] = from[n]; n++; }
	to[n] = 0;
	return n;
}

static uint8_t SEC_DANA same(const char *a, const char *b)
{
	while (*a && *a == *b) { a++; b++; }
	return *a == *b;
}

static void SEC_DANA face(Surface *s)
{
	ui_fill(s, 0, 0, s->lw, s->lh, UIC_PAPER);
	ui_bevel(s, 0, 0, s->lw, s->lh, UIC_WHITE, UIC_PAPER_DK);
}

/* The same raised buttons and recessed readouts as the main panel. */
static void SEC_DANA box(Surface *s, int16_t x, int16_t y, int16_t w, const char *t, uint8_t active)
{
	int16_t n = 0, room = (w - 4) / UI_CHAR;
	while (n < room && t[n]) n++;
	ui_fill(s, x, y, w, BOX_H, active ? UIC_BRASS : UIC_TITLE);
	ui_bevel(s, x, y, w, BOX_H, active ? UIC_BRASS_LT : UIC_INK, active ? UIC_BRASS_DK : UIC_WHITE);
	ui_ctext(s, x + w / 2, y + 3, t, n, active ? UIC_INK : UIC_PAPER, -1);
}

static void SEC_DANA well(Surface *s, int16_t y, const char *t, uint8_t active)
{
	box(s, 2, y, RAIL_IN, t, active);
}

static uint8_t SEC_DANA green_practice(const Play *p)
{
	return p->round && p->round->mode == MODE_GREEN;
}

static uint8_t SEC_DANA can_card(const Play *p)
{
	return p->round && !MODE_PRACTICE(p->round->mode);
}

static Surface SEC_DANA map_surface(const Surface *left)
{
	Surface s = *left;
	s.bits += MULU16(MAP_Y, s.rowBytes);
	s.h = s.lh = MAP_H;
	s.fullview = 1;
	return s;
}

static uint8_t SEC_DANA can_book(uint8_t state, uint8_t view, uint8_t cpu)
{
	return state == PS_AIM && view != PLAY_CARD && !cpu;
}

static void SEC_DANA left_labels(Surface *s, const Play *p)
{
	const Hole *h = p->rv.hole;
	char t[16];
	int16_t n;

	play_num(t + append(t, "Hole "), h->number);
	well(s, 2, t, 0);
	n = append(t, "Par ");
	n += play_num(t + n, h->par);
	n += append(t + n, " ");
	n += play_num(t + n, h->tees[h->tee].yards);
	append(t + n, "y");
	well(s, 16, t, 0);
	n = append(t, "Wind ");
	if (p->wind_mph) {
		uint8_t dir = (uint8_t)(((uint16_t)(p->wind_dir - ANG_90 + 4096) >> 13) & 7);
		n += append(t + n, dirs[dir]);
		n += append(t + n, " ");
	}
	play_num(t + n, p->wind_mph);
	well(s, WIND_Y, t, 0);
	ui_fill(s, 2, BOOK_Y, RAIL_IN, BOX_H, UIC_PAPER);
	if (can_book(p->state, p->view, p->cpu)) well(s, BOOK_Y, "Book", 1);
}

static vec2 SEC_DANA map_target(const Play *p, uint16_t carry)
{
	fixed r = (fixed)carry << 16;
	if (p->target_set) return p->target;
	if (p->club == CLUB_PT) r = p->range;
	return vec2_make(p->rv.ball.x + fix_mulq14(r, icos(p->aim)),
	                 p->rv.ball.y + fix_mulq14(r, isin(p->aim)));
}

static void SEC_DANA locate(const RenderMap *m, const BallPt *p, int16_t *x, int16_t *y)
{
	*x = (int16_t)(m->left + ((fixed)p->x * 4096) / m->ypp);
	*y = (int16_t)(m->top + (m->length - (fixed)p->y * 4096) / m->ypp);
}

static void SEC_DANA trace(Surface *s, const RenderMap *m, const BallPt *from, const BallPt *to)
{
	int16_t x0, y0, x1, y1, dx, dy, n, i;
	locate(m, from, &x0, &y0);
	locate(m, to, &x1, &y1);
	dx = x1 - x0;
	dy = y1 - y0;
	n = dx < 0 ? -dx : dx;
	if (dy > n || -dy > n) n = dy < 0 ? -dy : dy;
	if (n > 400) return;
	if (!n) {
		gfx_fill(s, x0, y0, 1, 1, UIC_WHITE);
		return;
	}
	for (i = 0; i <= n; i++)
		gfx_fill(s, x0 + (int16_t)((int32_t)dx * i / n),
		         y0 + (int16_t)((int32_t)dy * i / n), 1, 1, UIC_WHITE);
}

/* Restore the old marker before drawing points over that part of the trail. */
static void SEC_DANA unmark(DanaPanels *d, Surface *s)
{
	uint8_t i;
	if (!d->marker_count) return;
	for (i = 0; i < 3; i++) {
		uint8_t *q = s->bits + MULU16(d->marker_y + i, s->rowBytes) + d->marker_first;
		uint8_t k;
		for (k = 0; k < d->marker_count; k++) q[k] = d->marker_bytes[i * 3 + k];
	}
	d->marker_count = 0;
}

static void SEC_DANA mark(DanaPanels *d, Surface *s, const BallPt *p)
{
	int16_t x, y, first, last;
	uint8_t i;
	locate(&d->map, p, &x, &y);
	if (x <= 0 || y <= 0 || x + 1 >= s->w || y + 1 >= s->h) return;
	first = (int16_t)(((uint16_t)(x - 1) << s->lg) >> 3);
	last = (int16_t)(((uint16_t)(x + 1) << s->lg) >> 3);
	d->marker_first = (uint8_t)first;
	d->marker_count = (uint8_t)(last - first + 1);
	d->marker_y = y - 1;
	for (i = 0; i < 3; i++) {
		const uint8_t *q = s->bits + MULU16(y - 1 + i, s->rowBytes) + first;
		uint8_t k;
		for (k = 0; k < d->marker_count; k++) d->marker_bytes[i * 3 + k] = q[k];
	}
	gfx_fill(s, x - 1, y - 1, 3, 3, UIC_BLACK);
	gfx_fill(s, x, y, 1, 1, UIC_WHITE);
}

static uint8_t SEC_DANA left_draw(DanaPanels *d, Surface *s, const Play *p, uint16_t carry)
{
	const Hole *h = p->rv.hole;
	int16_t point = -1;
	uint8_t change = can_book(d->state, d->view, d->cpu) != can_book(p->state, p->view, p->cpu);
	Surface pane = map_surface(s);
	vec2 target = map_target(p, carry);
	uint8_t base = !d->map_ready || h != d->hole || h->number != d->hole_number ||
	               h->tee != d->tee || d->ball.x != p->rv.ball.x || d->ball.y != p->rv.ball.y ||
	               d->target.x != target.x || d->target.y != target.y || d->pin != p->rv.pin ||
	               d->map_stroke != p->stroke || d->map_player != p->cur;

	if (p->state >= PS_FLY && p->have_shot && p->shot.n)
		point = p->state == PS_FLY ? p->shown : p->shot.n - 1;
	if (point < 0) point = -1;
	if (point < d->shown) base = 1;
	if (base) {
		face(s);
		d->map_ready = render_target_map(&pane, &p->rv, &d->map, &target) == 0;
		d->marker_count = 0;
		d->shown = -1;
		d->hole = h;
		d->hole_number = h->number;
		d->tee = h->tee;
		d->ball = p->rv.ball;
		d->target = target;
		d->pin = p->rv.pin;
		d->map_stroke = p->stroke;
		d->map_player = p->cur;
	}
	if (base || change || d->wind != p->wind_mph || d->direction != p->wind_dir)
		left_labels(s, p);
	else if (point == d->shown) return 0;
	if (d->map_ready && point >= 0 && point < p->shot.n && point > d->shown) {
		int16_t i = d->shown < 1 ? 1 : d->shown + 1;
		unmark(d, &pane);
		for (; i <= point; i++) trace(&pane, &d->map, &p->shot.path[i - 1], &p->shot.path[i]);
		if (p->shot.result != SHOT_HOLED || point < p->shot.n - 1)
			mark(d, &pane, &p->shot.path[point]);
		d->shown = point;
	}
	d->wind = p->wind_mph;
	d->direction = p->wind_dir;
	return DANA_LEFT;
}

static int16_t SEC_DANA meter_x(int16_t pos)
{
	return (int16_t)(4 + (int32_t)pos * (DANA_RAIL_W - 8) / (MTR_W * 16));
}

static void SEC_DANA meter(Surface *s, const Play *p)
{
	int16_t snap = meter_x(MTR_SNAP * 16);
	int16_t mark_x = meter_x(p->mpos), power_x = meter_x(p->state == PS_UP ? p->mpos : p->turn);
	ui_fill(s, 2, DANA_METER_Y, RAIL_IN, DANA_METER_H, UIC_TITLE);
	ui_bevel(s, 2, DANA_METER_Y, RAIL_IN, DANA_METER_H, UIC_INK, UIC_WHITE);
	ui_fill(s, 3, DANA_METER_Y + 3, RAIL_IN - 2, 7, UIC_BLACK);
	if (power_x > snap) ui_fill(s, snap, DANA_METER_Y + 4, power_x - snap, 5, UIC_BRASS);
	ui_fill(s, snap, DANA_METER_Y + 2, 1, BOX_H - 4, UIC_TEXT_HI);
	if (p->club == CLUB_PT) {
		int16_t pace = meter_x((MTR_SNAP + p->putt_tick) * 16);
		ui_fill(s, pace, DANA_METER_Y + 8, 1, 4, UIC_TEE);
	}
	ui_fill(s, mark_x, DANA_METER_Y + 2, 1, BOX_H - 4, UIC_WHITE);
}

static int16_t SEC_DANA wrap(Surface *s, int16_t y, const char *t)
{
	char line[12];
	int16_t n, space, i;
	while (*t) {
		space = 0;
		for (n = 0; n < 11 && t[n]; n++)
			if (t[n] == ' ') space = n;
		if (t[n] && t[n] != ' ' && space) n = space;
		for (i = 0; i < n; i++) line[i] = t[i];
		line[n] = 0;
		well(s, y, line, 0);
		y += 14;
		t += n;
		while (*t == ' ') t++;
	}
	return y;
}

static uint8_t SEC_DANA card_back(const Play *p)
{
	return p->card == CARD_CLUB || p->card == CARD_BOOK || p->card == CARD_STATS || p->card == CARD_VIEW;
}

static void SEC_DANA right_body(Surface *s, const Play *p, const char *action, uint16_t distance, uint16_t carry)
{
	char t[18];
	int16_t n;
	uint8_t aim = p->state == PS_AIM && !p->cpu;
	uint8_t swing = p->state == PS_UP || p->state == PS_DOWN;

	face(s);
	if (p->view == PLAY_CARD) {
		const char *title = p->card == CARD_CLUB ? "Clubs" : p->card == CARD_BOOK ? "Yardage" :
		                    p->card == CARD_STATS ? "Round stats" : p->board ? "Leaders" : "Scorecard";
		well(s, 2, title, 0);
		if (p->round) {
			uint8_t player = p->card == CARD_STATS ? p->stat_player : p->cur;
			well(s, PIN_Y, p->round->pl[player].name, 0);
		}
		if (card_back(p)) well(s, NAV_Y, p->card == CARD_BOOK ? "Map" : "Back", 1);
		well(s, ACT_Y, "Select", 1);
		return;
	}
	play_num(t + append(t, "Shot "), p->stroke);
	well(s, 2, t, 0);
	n = append(t, p->club == CLUB_PT ? "Cup " : "Pin ");
	n += play_num(t + n, distance);
	append(t + n, p->club == CLUB_PT ? " ft" : "y");
	well(s, PIN_Y, t, 0);
	if (p->state >= PS_RESULT) {
		int16_t y = wrap(s, 47, p->msg1);
		wrap(s, y + 5, p->msg2);
		if (action) well(s, ACT_Y, action, 1);
		return;
	}
	n = append(t, ball_club_name(p->club));
	if (p->club != CLUB_PT) {
		n += append(t + n, " ");
		play_num(t + n, carry);
	}
	well(s, CLUB_Y, t, aim && p->view == PLAY_SCENE && p->lie != LIE_GREEN);
	well(s, LIE_Y, ball_lie_name(p->lie), 0);
	if (p->club != CLUB_PT) well(s, SHAPE_Y, ball_shape_name(p->shape), aim && p->view == PLAY_SCENE);
	if (swing) {
		meter(s, p);
		well(s, ACT_Y, p->state == PS_UP ? "Power" : "Snap", 1);
	} else if (aim && p->view == PLAY_SCENE) {
		box(s, 2, NAV_Y, 44, "Map", 1);
		if (can_card(p)) box(s, 50, NAV_Y, 44, "Card", 1);
		if (green_practice(p)) box(s, 50, NAV_Y, 44, p->round->repeat ? "Rpt+" : "Rpt", 1);
		well(s, ACT_Y, p->club == CLUB_PT ? "Putt" : "Swing", 1);
	} else if (aim) {
		if (can_card(p)) box(s, 50, NAV_Y, 44, "Card", 1);
		well(s, ACT_Y, "Back", 1);
	}
}

static uint8_t SEC_DANA right_draw(DanaPanels *d, Surface *s, const Play *p, uint16_t distance, uint16_t carry)
{
	const char *action = play_dana_action(p);
	uint8_t repeat = green_practice(p) && p->round->repeat;
	uint8_t swing = p->state == PS_UP || p->state == PS_DOWN;
	uint8_t changed = DANA_RIGHT;
	uint8_t new_body = !d->right_ready || d->view != p->view || d->state != p->state ||
	                   d->club != p->club || d->lie != p->lie || d->shape != p->shape ||
	                   d->stroke != p->stroke || d->player != p->cur || d->repeat != repeat ||
	                   (p->view == PLAY_CARD && (d->card != p->card || d->board != p->board ||
	                    (p->card == CARD_STATS && d->card_player != p->stat_player))) ||
	                   d->cpu != p->cpu || d->action != action || d->distance != distance ||
	                   d->carry != carry || !same(d->msg1, p->msg1) || !same(d->msg2, p->msg2);
	if (new_body) {
		right_body(s, p, action, distance, carry);
		d->view = p->view;
		d->state = p->state;
		d->club = p->club;
		d->lie = p->lie;
		d->shape = p->shape;
		d->stroke = p->stroke;
		d->player = p->cur;
		d->repeat = repeat;
		d->cpu = p->cpu;
		d->action = action;
		d->card = p->card;
		d->board = p->board;
		d->card_player = p->stat_player;
		d->distance = distance;
		d->carry = carry;
		append(d->msg1, p->msg1);
		append(d->msg2, p->msg2);
		d->right_ready = 1;
	} else if (swing && (d->meter != p->mpos || d->turn != p->turn || d->putt_tick != p->putt_tick)) {
		meter(s, p);
		changed = DANA_METER;
	} else return 0;
	d->meter = p->mpos;
	d->turn = p->turn;
	d->putt_tick = p->putt_tick;
	return changed;
}

void dana_reset(DanaPanels *d)
{
	d->map_ready = d->right_ready = d->marker_count = 0;
	d->state = d->view = d->cpu = 0;
	d->shown = -1;
}

uint8_t dana_draw(DanaPanels *d, Surface *left, Surface *right, const Play *p)
{
	uint16_t carry = ball_shape_carry(p->club, p->lie, p->shape);
	uint8_t changed = left_draw(d, left, p, carry);
	return changed | right_draw(d, right, p, play_dana_distance(p), carry);
}

static uint8_t SEC_DANA on_row(int16_t y, int16_t top)
{
	return y >= top && y < top + BOX_H;
}

int dana_tap(const DanaPanels *d, Play *p, uint8_t pane, int16_t x, int16_t y, uint32_t now)
{
	if (x < 2 || x >= DANA_RAIL_W - 2) return 0;
	if (pane == DANA_LEFT && can_book(p->state, p->view, p->cpu)) {
		if (on_row(y, BOOK_Y)) {
			if (p->view == PLAY_SCENE) play_key(p, KEY_MAP, now);
			return play_key(p, KEY_UP, now);
		}
		if (d->map_ready && y >= MAP_Y && y < MAP_Y + MAP_H) {
			vec2 point;
			point.x = fix_muli(d->map.ypp, x - d->map.left) + d->map.ypp / 2;
			point.y = d->map.length - fix_muli(d->map.ypp, y - MAP_Y - d->map.top) - d->map.ypp / 2;
			return play_dana_aim(p, point);
		}
	}
	if (pane == DANA_RIGHT) {
		if (p->view == PLAY_CARD && on_row(y, NAV_Y) && card_back(p))
			return play_key(p, p->card == CARD_VIEW ? KEY_CARD : KEY_BACK, now);
		if (on_row(y, ACT_Y) || ((p->state == PS_UP || p->state == PS_DOWN) && on_row(y, DANA_METER_Y)))
			return play_key(p, KEY_SELECT, now);
		if (p->state != PS_AIM || p->cpu || p->view == PLAY_CARD) return 0;
		if (on_row(y, CLUB_Y) && p->view == PLAY_SCENE) return play_dana_clubs(p);
		if (on_row(y, SHAPE_Y) && p->view == PLAY_SCENE && p->club != CLUB_PT)
			return play_key(p, KEY_SHAPE, now);
		if (on_row(y, NAV_Y)) {
			if (x < 46 && p->view == PLAY_SCENE) return play_key(p, KEY_MAP, now);
			if (x >= 50 && (can_card(p) || (green_practice(p) && p->view == PLAY_SCENE)))
				return play_key(p, KEY_CARD, now);
		}
	}
	return 0;
}
