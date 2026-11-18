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
static void SEC_UI page_holes(const Round *r, uint8_t page, uint8_t *from, uint8_t *to)
{
	*from = (uint8_t)(page * 9);
	*to = (uint8_t)(*from + 9 > r->holes ? r->holes : *from + 9);
	if (*from > *to) *from = *to;
}

/* Lines under the holes: Out or In on a course of more than 9, then Tot and +/-. */
static uint8_t SEC_UI sums(const Round *r)
{
	return (uint8_t)(r->holes > 9 ? 3 : 2);
}

/* The buttons: what the first does, then the other nine on 18 holes, or
 * the next nine past that. */
static const char *SEC_UI buttons(const Round *r, uint8_t mode, uint8_t page)
{
	if (r->holes <= 9) return mode == CARD_FINAL ? "Done" : mode == CARD_HOLE ? "Next hole" : "OK";
	if (r->holes > 18) return mode == CARD_FINAL ? "Done|Next 9" : mode == CARD_HOLE ? "Next hole|Next 9" : "OK|Next 9";
	if (mode == CARD_FINAL) return page ? "Done|Front 9" : "Done|Back 9";
	if (mode == CARD_HOLE) return page ? "Next hole|Front 9" : "Next hole|Back 9";
	return page ? "OK|Front 9" : "OK|Back 9";
}

void card_layout(UiDialog *d, const Round *r, uint8_t mode, uint8_t page)
{
	uint8_t from, to, i;
	int16_t w, bw = 0, x, n;

	page_holes(r, page, &from, &to);
	d->title = mode == CARD_FINAL ? "Final card" : "Scorecard";
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
static int16_t SEC_UI num(char *t, int16_t v, uint8_t sign)
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
static void SEC_UI rtext(Surface *s, int16_t x, int16_t y, const char *t, int16_t n)
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
	/* the heading: Par and the players by number */
	rtext(s, tx + PAR_R, y, "Par", 3);
	for (i = 0; i < r->players; i++) {
		t[0] = 'P';
		t[1] = (char)('1' + i);
		rtext(s, (int16_t)(tx + PAR_R + COL_W * (i + 1)), y, t, 2);
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
		/* Out or In over this page's holes, then Tot over all of them, then against par */
		uint8_t a = 0, z = r->holes, kind = (uint8_t)(k + 3 - sums(r));
		const char *l = kind == 0 ? (page == 0 ? "Out" : page == 1 ? "In" : "Sub") : kind == 1 ? "Tot" : "+/-";
		if (kind == 0) { a = from; z = to; }
		for (n = 0; l[n]; n++) {}
		ui_text(s, tx, y, l, n, UIC_INK, -1);
		if (kind < 2) {
			n = num(t, rules_par(r, a, z), 0);
			rtext(s, tx + PAR_R, y, t, n);
		}
		for (i = 0; i < r->players; i++) {
			if (!rules_played(r, i, a, z)) continue;
			n = kind < 2 ? num(t, rules_strokes(r, i, a, z), 0) : num(t, rules_to_par(r, i), 1);
			rtext(s, (int16_t)(tx + PAR_R + COL_W * (i + 1)), y, t, n);
		}
		y += CARD_ROW;
	}
}
