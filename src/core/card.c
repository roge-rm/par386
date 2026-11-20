#include "card.h"

/* As in ui.c's dialog. */
#define TITLE_H 12
#define BTN_H   14
#define GAP     3
#define LABEL_W 24    /* the hole or Out, In, Tot */
#define PAR_R   44    /* the par column's right edge, from the table's left */
#define COL_W   28    /* each player's column */

uint8_t card_pages(const Round *r)
{
	return (uint8_t)((r->holes + 8) / 9);
}

/* Holes on page: from and to, 0-based, to not included. */
static void SEC_MENU page_holes(const Round *r, uint8_t page, uint8_t *from, uint8_t *to)
{
	*from = (uint8_t)(page * 9);
	*to = (uint8_t)(*from + 9 > r->holes ? r->holes : *from + 9);
	if (*from > *to) *from = *to;
}

/* The title, made for each layout. */
static char title[24];

/* Lines under the holes: Out or In on a course of more than 9, then Tot
 * and +/- (skins won in a skins game, for room). */
static uint8_t SEC_MENU sums(const Round *r)
{
	return (uint8_t)(r->holes > 9 ? 3 : 2);
}

/* The buttons: what the first does, then the other nine on 18 holes, or
 * the next nine past that, then in a tournament the leaderboard. */
static const char *SEC_MENU buttons(const Round *r, uint8_t mode, uint8_t page)
{
	if (mode == CARD_FINAL && r->tour_stage == TOUR_BREAK) {
		if (r->holes <= 9) return "Round 2";
		if (r->holes > 18) return "Round 2|Next 9";
		return page ? "Round 2|Front 9" : "Round 2|Back 9";
	}
	if (r->mode == MODE_TOURN && mode == CARD_VIEW) {
		if (r->holes <= 9) return "OK|Board";
		if (r->holes > 18) return "OK|Next 9|Board";
		return page ? "OK|Front 9|Board" : "OK|Back 9|Board";
	}
	if (r->holes <= 9) return mode == CARD_FINAL ? "Done" : mode == CARD_HOLE ? "Next hole" : "OK";
	if (r->holes > 18) return mode == CARD_FINAL ? "Done|Next 9" : mode == CARD_HOLE ? "Next hole|Next 9" : "OK|Next 9";
	if (mode == CARD_FINAL) return page ? "Done|Front 9" : "Done|Back 9";
	if (mode == CARD_HOLE) return page ? "Next hole|Front 9" : "Next hole|Back 9";
	return page ? "OK|Front 9" : "OK|Back 9";
}

uint8_t card_buttons(const Round *r, uint8_t mode)
{
	return ui_parts(buttons(r, mode, 0), '|');
}

uint8_t card_board_button(const Round *r, uint8_t mode)
{
	return (uint8_t)(r->mode == MODE_TOURN && mode == CARD_VIEW ? card_buttons(r, mode) - 1 : 0xFF);
}

static void SEC_MENU add(char *t, uint8_t *n, const char *s)
{
	while (*s && *n < sizeof(title) - 1) t[(*n)++] = *s++;
	t[*n] = 0;
}

/* Writes v in decimal at t[*n]. */
static void SEC_MENU addn(char *t, uint8_t *n, uint8_t v)
{
	if (v >= 10) t[(*n)++] = (char)('0' + v / 10);
	t[(*n)++] = (char)('0' + v % 10);
	t[*n] = 0;
}

/* Default names use the short slot label on the card. */
static const char player_prefix[] = "Player ";

static uint8_t SEC_MENU named(const RoundPlayer *p, uint8_t slot)
{
	uint8_t k;

	for (k = 0; player_prefix[k]; k++)
		if (p->name[k] != player_prefix[k]) return 1;
	return p->name[k] != '1' + slot || p->name[k + 1];
}

/* A side in match play ("P1") or best ball ("P1+P2"). */
static void SEC_MENU side_name(const Round *r, uint8_t side, char *t, uint8_t *n)
{
	if (r->mode == MODE_MATCH && named(&r->pl[side], side)) {
		const char *name = r->pl[side].name;
		uint8_t k;
		for (k = 0; k < 6 && name[k]; k++) t[(*n)++] = name[k];
		t[*n] = 0;
		return;
	}
	add(t, n, side ? (r->mode == MODE_BEST ? "P3+P4" : "P2") : (r->mode == MODE_BEST ? "P1+P2" : "P1"));
}

/* The card's title: the match's standing or result, the skins carried, or
 * plain "Scorecard" and "Final card". */
static const char *SEC_MENU card_title(const Round *r, uint8_t mode)
{
	uint8_t n = 0;

	title[0] = 0;
	if (r->mode == MODE_MATCH || r->mode == MODE_BEST) {
		uint8_t left, a;
		int8_t up = rules_match(r, &left);
		a = (uint8_t)(up < 0 ? -up : up);
		if (!up) {
			add(title, &n, r->over ? "Match halved" : "All square");
			return title;
		}
		side_name(r, up < 0, title, &n);
		if (r->over) {
			/* "P1 wins 3 & 2", or on the last hole "P1 wins 2 up" */
			add(title, &n, r->mode == MODE_BEST ? " win " : " wins ");
			addn(title, &n, a);
			if (left) {
				add(title, &n, " & ");
				addn(title, &n, left);
			} else {
				add(title, &n, " up");
			}
		} else {
			add(title, &n, " ");
			addn(title, &n, a);
			add(title, &n, " up");
		}
		return title;
	}
	if (r->mode == MODE_SKINS && !r->over) {
		uint8_t won[RULES_PLAYERS], carry;
		rules_skins(r, won, &carry);
		if (carry) {
			add(title, &n, "Skins, ");
			addn(title, &n, carry);
			add(title, &n, " carried");
			return title;
		}
	}
	if (r->tour_stage == TOUR_FIRST || r->tour_stage == TOUR_BREAK) return "Round 1 card";
	if (r->tour_stage == TOUR_SECOND) return "Round 2 card";
	return mode == CARD_FINAL ? "Final card" : "Scorecard";
}

void card_layout(UiDialog *d, const Round *r, uint8_t mode, uint8_t page)
{
	uint8_t from, to, i;
	int16_t w, bw = 0, x, n;

	page_holes(r, page, &from, &to);
	d->title = card_title(r, mode);
	d->text = "";
	d->buttons = buttons(r, mode, page);
	d->count = ui_parts(d->buttons, '|');
	d->sel = 0;
	for (i = 0; i < d->count; i++) {
		ui_part(d->buttons, '|', i, &n);
		d->btn[i].w = (int16_t)(n * UI_CHAR + 8);
		if (d->btn[i].w < 36) d->btn[i].w = 36;
		bw += d->btn[i].w + (i ? 6 : 0);
	}
	w = (int16_t)(PAR_R + COL_W * r->players + 4);
	if (w < bw + 8) w = (int16_t)(bw + 8);
	for (n = 0; d->title[n]; n++) {}
	if (w < n * UI_CHAR + 8) w = (int16_t)(n * UI_CHAR + 8);
	if (w < 96) w = 96;
	if (w > 160) w = 160;
	d->box.w = w;
	d->box.h = (int16_t)(1 + TITLE_H + GAP + CARD_ROW + 2 + (to - from) * CARD_ROW + 2 + sums(r) * CARD_ROW + GAP + BTN_H + GAP + 1);
	d->box.x = (int16_t)((160 - w) / 2);
	d->box.y = (int16_t)((160 - d->box.h) / 2);
	if (d->box.y < 0) d->box.y = 0;
	x = (int16_t)(d->box.x + (w - bw) / 2);
	for (i = 0; i < d->count; i++) {
		d->btn[i].x = x;
		d->btn[i].y = (int16_t)(d->box.y + d->box.h - 1 - GAP - BTN_H);
		d->btn[i].h = BTN_H;
		x += d->btn[i].w + 6;
	}
}

int card_hit(const Round *r, uint8_t mode, uint8_t page, int16_t x, int16_t y)
{
	UiDialog d;
	card_layout(&d, r, mode, page);
	return ui_dialog_hit(&d, x, y);
}

/* Writes v in decimal at t with a + or - if sign, returns the length. */
static int16_t SEC_MENU num(char *t, int16_t v, uint8_t sign)
{
	char b[6];
	int16_t n = 0, k = 0;
	uint16_t u = (uint16_t)(v < 0 ? -v : v);

	if (sign && v == 0) { t[0] = 'E'; t[1] = 0; return 1; }
	if (v < 0) t[k++] = '-';
	else if (sign) t[k++] = '+';
	do { b[n++] = (char)('0' + u % 10); u /= 10; } while (u && n < 5);
	while (n) t[k++] = b[--n];
	t[k] = 0;
	return k;
}

/* Text with its right edge at x. */
static void SEC_MENU rtext(Surface *s, int16_t x, int16_t y, const char *t, int16_t n)
{
	ui_text(s, (int16_t)(x - n * UI_CHAR), y, t, n, UIC_INK, -1);
}

void card_draw(Surface *s, const Round *r, uint8_t mode, uint8_t page, uint8_t sel)
{
	UiDialog d;
	uint8_t from, to, h, i, k;
	int16_t tx, y, n, line;
	char t[8];
	const URect *b;

	card_layout(&d, r, mode, page);
	d.sel = (uint8_t)(sel < d.count ? sel : 0);
	page_holes(r, page, &from, &to);
	b = &d.box;
	ui_dialog_draw(s, &d);

	tx = (int16_t)(b->x + (b->w - (PAR_R + COL_W * r->players)) / 2);
	y = (int16_t)(b->y + 1 + TITLE_H + GAP);
	/* The heading holds three letters per player. */
	rtext(s, tx + PAR_R, y, "Par", 3);
	for (i = 0; i < r->players; i++) {
		int16_t k = 2;
		t[0] = 'P';
		t[1] = (char)('1' + i);
		if (named(&r->pl[i], i)) {
			for (k = 0; k < 3 && r->pl[i].name[k]; k++) t[k] = r->pl[i].name[k];
		}
		rtext(s, (int16_t)(tx + PAR_R + COL_W * (i + 1)), y, t, k);
	}
	y += CARD_ROW;
	line = (int16_t)(tx + PAR_R + COL_W * r->players);
	ui_fill(s, tx, y, line - tx, 1, UIC_INK);
	y += 2;
	for (h = from; h < to; h++) {
		/* the hole being played on a light bar */
		if (h == r->hole && !r->over && mode == CARD_VIEW)
			ui_fill(s, tx - 1, y - 1, line - tx + 2, CARD_ROW, UIC_PAPER_DK);
		n = num(t, (int16_t)(h + 1), 0);
		ui_text(s, tx, y, t, n, UIC_INK, -1);
		n = num(t, r->pars[h], 0);
		rtext(s, tx + PAR_R, y, t, n);
		for (i = 0; i < r->players; i++) {
			if (!r->pl[i].score[h]) continue;
			n = num(t, r->pl[i].score[h], 0);
			rtext(s, (int16_t)(tx + PAR_R + COL_W * (i + 1)), y, t, n);
		}
		y += CARD_ROW;
	}
	ui_fill(s, tx, y, line - tx, 1, UIC_INK);
	y += 2;
	for (k = 0; k < sums(r); k++) {
		/* Out or In over this page's holes, then Tot over all of them, then against par or skins */
		uint8_t a = 0, z = r->holes, kind = (uint8_t)(k + 3 - sums(r));
		const char *l;
		if (kind == 2 && r->mode == MODE_SKINS) kind = 3;
		l = kind == 0 ? (page == 0 ? "Out" : page == 1 ? "In" : "Sub") : kind == 1 ? "Tot" : kind == 2 ? "+/-" : "Skn";
		if (kind == 0) { a = from; z = to; }
		for (n = 0; l[n]; n++) {}
		ui_text(s, tx, y, l, n, UIC_INK, -1);
		if (kind < 2) {
			n = num(t, rules_par(r, a, z), 0);
			rtext(s, tx + PAR_R, y, t, n);
		}
		if (kind == 3) {
			uint8_t won[RULES_PLAYERS], carry;
			rules_skins(r, won, &carry);
			for (i = 0; i < r->players; i++) {
				n = num(t, won[i], 0);
				rtext(s, (int16_t)(tx + PAR_R + COL_W * (i + 1)), y, t, n);
			}
			y += CARD_ROW;
			continue;
		}
		for (i = 0; i < r->players; i++) {
			if (!rules_played(r, i, a, z)) continue;
			n = kind < 2 ? num(t, rules_strokes(r, i, a, z), 0) : num(t, rules_to_par(r, i), 1);
			rtext(s, (int16_t)(tx + PAR_R + COL_W * (i + 1)), y, t, n);
		}
		y += CARD_ROW;
	}
}

/* ---- the tournament leaderboard ---- */

#define POS_W  24     /* "T10" */
#define NAME_W 64     /* 8 characters */
#define PAR_W  32     /* "+12" with a gap before it */

uint8_t board_buttons(const Round *r, uint8_t mode)
{
	(void)r;
	(void)mode;
	return 2;
}

void board_layout(UiDialog *d, const Round *r, uint8_t mode, uint8_t lines)
{
	uint8_t i, n = 0, done = rules_done(r);
	int16_t w, bw = 0, x, k;

	title[0] = 0;
	if (r->over) {
		add(title, &n, "Final leaders");
	} else if (r->tour_stage == TOUR_BREAK) {
		add(title, &n, "Round 1 leaders");
	} else {
		add(title, &n, r->tour_stage == TOUR_FIRST ? "Round 1" :
		              r->tour_stage == TOUR_SECOND ? "Round 2" : "Leaders");
		if (done) {
			add(title, &n, " after ");
			addn(title, &n, done);
		}
	}
	d->title = title;
	d->text = "";
	d->buttons = mode == CARD_FINAL ? (r->tour_stage == TOUR_BREAK ? "Round 2|Card" : "Done|Card") :
		mode == CARD_HOLE ? "Next hole|Card" : "OK|Card";
	d->count = ui_parts(d->buttons, '|');
	d->sel = 0;
	for (i = 0; i < d->count; i++) {
		ui_part(d->buttons, '|', i, &k);
		d->btn[i].w = (int16_t)(k * UI_CHAR + 8);
		if (d->btn[i].w < 36) d->btn[i].w = 36;
		bw += d->btn[i].w + (i ? 6 : 0);
	}
	w = POS_W + NAME_W + PAR_W + 12;
	if (w < bw + 8) w = (int16_t)(bw + 8);
	if (w < n * UI_CHAR + 8) w = (int16_t)(n * UI_CHAR + 8);
	d->box.w = w;
	d->box.h = (int16_t)(1 + TITLE_H + GAP + lines * CARD_ROW + GAP + BTN_H + GAP + 1);
	d->box.x = (int16_t)((160 - w) / 2);
	/* above the play panel (26 high) when it fits, so the hole's line stays in sight */
	d->box.y = (int16_t)(d->box.h <= 134 ? (134 - d->box.h) / 2 : (160 - d->box.h) / 2);
	if (d->box.y < 0) d->box.y = 0;
	x = (int16_t)(d->box.x + (w - bw) / 2);
	for (i = 0; i < d->count; i++) {
		d->btn[i].x = x;
		d->btn[i].y = (int16_t)(d->box.y + d->box.h - 1 - GAP - BTN_H);
		d->btn[i].h = BTN_H;
		x += d->btn[i].w + 6;
	}
}

void board_draw(Surface *s, const Round *r, uint8_t mode, uint8_t sel)
{
	UiDialog d;
	BoardLine b[RULES_PLAYERS + RULES_FIELD];
	uint8_t n = rules_board(r, b), i;
	int16_t x, y, k;
	char t[8];

	board_layout(&d, r, mode, n);
	d.sel = (uint8_t)(sel < d.count ? sel : 0);
	ui_dialog_draw(s, &d);
	x = (int16_t)(d.box.x + (d.box.w - (POS_W + NAME_W + PAR_W)) / 2);
	y = (int16_t)(d.box.y + 1 + TITLE_H + GAP);
	for (i = 0; i < n; i++, y += CARD_ROW) {
		const char *nm = b[i].who < RULES_PLAYERS ? r->pl[b[i].who].name : rules_field_name((uint8_t)(b[i].who - RULES_PLAYERS));
		/* the round's players on a light bar */
		if (b[i].who < RULES_PLAYERS) ui_fill(s, x - 1, y - 1, POS_W + NAME_W + PAR_W + 2, CARD_ROW, UIC_PAPER_DK);
		k = 0;
		if (b[i].tied) t[k++] = 'T';
		k = (int16_t)(k + num(t + k, b[i].pos, 0));
		ui_text(s, x, y, t, k, UIC_INK, -1);
		for (k = 0; nm[k] && k < 8; k++) {}
		ui_text(s, x + POS_W, y, nm, k, UIC_INK, -1);
		k = num(t, b[i].to_par, 1);
		rtext(s, x + POS_W + NAME_W + PAR_W, y, t, k);
	}
}

int board_hit(const Round *r, uint8_t mode, int16_t x, int16_t y)
{
	UiDialog d;
	board_layout(&d, r, mode, (uint8_t)(r->players + RULES_FIELD));
	return ui_dialog_hit(&d, x, y);
}
