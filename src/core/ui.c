#include "ui.h"

#define TITLE_H 12
#define PAD 6
#define LINE_H 10
#define BTN_H 14
#define BTN_GAP 6
#define BAR_H 14

const char *ui_part(const char *list, char sep, uint8_t i, int16_t *len)
{
	const char *p = list;
	int16_t n;

	while (i && *p) {
		if (*p++ == sep) i--;
	}
	for (n = 0; p[n] && p[n] != sep; n++) {}
	*len = n;
	return p;
}

uint8_t ui_parts(const char *list, char sep)
{
	uint8_t n = 1;

	while (*list)
		if (*list++ == sep) n++;
	return n;
}

void ui_fill(Surface *s, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c)
{
	gfx_fill(s, s->ox + x * s->scale, s->oy + y * s->scale, w * s->scale, h * s->scale, c);
}

void ui_text(Surface *s, int16_t x, int16_t y, const char *t, int16_t n, uint8_t c, int8_t shadow)
{
	/* No shadow where it would be the text's own colour, as on 4 greys. */
	if (shadow >= 0 && (s->pa[shadow] != s->pa[c] || s->pb[shadow] != s->pb[c]))
		gfx_text(s, s->ox + (x + 1) * s->scale, s->oy + (y + 1) * s->scale, t, n, (uint8_t)shadow);
	gfx_text(s, s->ox + x * s->scale, s->oy + y * s->scale, t, n, c);
}

void ui_ctext(Surface *s, int16_t cx, int16_t y, const char *t, int16_t n, uint8_t c, int8_t shadow)
{
	ui_text(s, cx - n * UI_CHAR / 2, y, t, n, c, shadow);
}

void ui_bevel(Surface *s, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t lt, uint8_t dk)
{
	ui_fill(s, x, y, w, 1, lt);
	ui_fill(s, x, y, 1, h, lt);
	ui_fill(s, x, y + h - 1, w, 1, dk);
	ui_fill(s, x + w - 1, y + 1, 1, h - 1, dk);
}

void ui_menu_init(UiMenu *m, const char *items, int16_t x, int16_t y, int16_t w, int16_t lineH)
{
	m->items = items;
	m->count = ui_parts(items, '|');
	if (m->count > UI_MAX_ITEMS) m->count = UI_MAX_ITEMS;
	m->sel = 0;
	m->x = x;
	m->y = y;
	m->w = w;
	m->lineH = lineH;
}

void ui_menu_move(UiMenu *m, int8_t d)
{
	int16_t n = (int16_t)m->sel + d;

	while (n < 0) n += m->count;
	m->sel = (uint8_t)(n % m->count);
}

/* Each line's tap area is the bar's width and the line height, centred on the text. */
int ui_menu_hit(const UiMenu *m, int16_t x, int16_t y)
{
	int16_t top = m->y + UI_CHAR / 2 - m->lineH / 2, i;

	if (x < m->x || x >= m->x + m->w || y < top) return -1;
	i = (y - top) / m->lineH;
	return i < m->count ? i : -1;
}

void ui_menu_line_rect(const UiMenu *m, uint8_t i, URect *r)
{
	r->x = m->x;
	r->y = m->y + i * m->lineH - (BAR_H - UI_CHAR) / 2 - 1;
	r->w = m->w;
	r->h = BAR_H;
}

void ui_menu_draw_line(Surface *s, const UiMenu *m, uint8_t i)
{
	int16_t n, ty = m->y + i * m->lineH;
	const char *t = ui_part(m->items, '|', i, &n);
	URect r;

	if (i == m->sel) {
		ui_menu_line_rect(m, i, &r);
		ui_fill(s, r.x, r.y, r.w, r.h, UIC_BAR);
		ui_fill(s, r.x, r.y, r.w, 1, UIC_BAR_LT);
		ui_fill(s, r.x, r.y + r.h - 1, r.w, 1, UIC_BAR_DK);
		gfx_ball(s, s->ox + (m->x + 3) * s->scale, s->oy + (r.y + (r.h - gfx_ball_h()) / 2) * s->scale);
	}
	ui_text(s, m->x + 14, ty, t, n, i == m->sel ? UIC_TEXT_HI : UIC_TEXT, UIC_SHADOW);
}

void ui_menu_draw(Surface *s, const UiMenu *m)
{
	uint8_t i;

	for (i = 0; i < m->count; i++)
		ui_menu_draw_line(s, m, i);
}

void ui_dialog_init(UiDialog *d, const char *title, const char *text, const char *buttons)
{
	uint8_t i, lines = ui_parts(text, '\n');
	int16_t n, w = 0, bw = 0, x;

	d->title = title;
	d->text = text;
	d->buttons = buttons;
	d->count = ui_parts(buttons, '|');
	if (d->count > UI_MAX_BUTTONS) d->count = UI_MAX_BUTTONS;
	d->sel = 0;

	for (i = 0; i < lines; i++) {
		ui_part(text, '\n', i, &n);
		if (n * UI_CHAR > w) w = n * UI_CHAR;
	}
	ui_part(title, 0, 0, &n);
	if (n * UI_CHAR + 8 > w) w = n * UI_CHAR + 8;
	for (i = 0; i < d->count; i++) {
		ui_part(buttons, '|', i, &n);
		d->btn[i].w = n * UI_CHAR + 12;
		if (d->btn[i].w < 36) d->btn[i].w = 36;
		bw += d->btn[i].w + (i ? BTN_GAP : 0);
	}
	if (bw > w) w = bw;
	w += 2 * PAD + 2;
	if (w > 156) w = 156;
	d->box.w = w;
	d->box.h = 1 + TITLE_H + PAD + lines * LINE_H + 2 + BTN_H + PAD + 1;
	d->box.x = (160 - w) / 2;
	d->box.y = (160 - d->box.h) / 2;

	x = d->box.x + (w - bw) / 2;
	for (i = 0; i < d->count; i++) {
		d->btn[i].x = x;
		d->btn[i].y = d->box.y + d->box.h - 1 - PAD - BTN_H;
		d->btn[i].h = BTN_H;
		x += d->btn[i].w + BTN_GAP;
	}
}

int ui_dialog_hit(const UiDialog *d, int16_t x, int16_t y)
{
	uint8_t i;

	for (i = 0; i < d->count; i++) {
		const URect *r = &d->btn[i];
		/* A little slack round each button for fingers. */
		if (x >= r->x - 2 && x < r->x + r->w + 2 && y >= r->y - 2 && y < r->y + r->h + 2)
			return i;
	}
	return -1;
}

void ui_dialog_draw(Surface *s, const UiDialog *d)
{
	const URect *b = &d->box;
	uint8_t i, lines = ui_parts(d->text, '\n');
	int16_t n, cx = b->x + b->w / 2;
	const char *t;

	ui_fill(s, b->x + 3, b->y + 3, b->w, b->h, UIC_BLACK);
	ui_fill(s, b->x, b->y, b->w, b->h, UIC_BLACK);
	ui_fill(s, b->x + 1, b->y + 1, b->w - 2, b->h - 2, UIC_PAPER);
	ui_bevel(s, b->x + 1, b->y + 1 + TITLE_H, b->w - 2, b->h - 2 - TITLE_H, UIC_WHITE, UIC_PAPER_DK);

	ui_fill(s, b->x + 1, b->y + 1, b->w - 2, TITLE_H, UIC_TITLE);
	ui_fill(s, b->x + 1, b->y + TITLE_H, b->w - 2, 1, UIC_BRASS);
	t = ui_part(d->title, 0, 0, &n);
	ui_ctext(s, cx, b->y + 3, t, n, UIC_PAPER, UIC_SHADOW);

	for (i = 0; i < lines; i++) {
		t = ui_part(d->text, '\n', i, &n);
		ui_ctext(s, cx, b->y + 1 + TITLE_H + PAD + i * LINE_H, t, n, UIC_INK, -1);
	}

	for (i = 0; i < d->count; i++) {
		const URect *r = &d->btn[i];
		if (i == d->sel)
			ui_fill(s, r->x - 1, r->y - 1, r->w + 2, r->h + 2, UIC_BLACK);
		ui_fill(s, r->x, r->y, r->w, r->h, UIC_BRASS);
		ui_bevel(s, r->x, r->y, r->w, r->h, UIC_BRASS_LT, UIC_BRASS_DK);
		t = ui_part(d->buttons, '|', i, &n);
		ui_ctext(s, r->x + r->w / 2, r->y + (BTN_H - UI_CHAR) / 2, t, n, UIC_INK, -1);
	}
}
