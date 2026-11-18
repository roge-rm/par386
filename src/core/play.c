#include "play.h"

void play_init(Play *p, const Hole *h, const SurfaceGrid *g)
{
	p->rv.hole = h;
	p->rv.grid = g;
	p->rv.pin = 0;
	p->view = PLAY_SCENE;
	play_spot(p, 0);
}

void play_spot(Play *p, uint8_t n)
{
	const Hole *h = p->rv.hole;
	vec2 to;

	p->spot = (uint8_t)(n % PLAY_SPOTS);
	switch (p->spot) {
	case 1: p->rv.ball = vec2_make(64L << 16, 190L << 16); break;
	case 2: p->rv.ball = vec2_make(124L << 16, 352L << 16); break;
	default: p->rv.ball = h->tees[0].pos; break;
	}
	to = p->spot ? h->pins[p->rv.pin & 3] : h->aim;
	p->rv.aim = vec2_angle(vec2_sub(to, p->rv.ball));
}

int play_key(Play *p, uint8_t key)
{
	switch (key) {
	case KEY_BACK:
		return PL_BACK;
	case KEY_SELECT: case KEY_OTHER:
		p->view = p->view == PLAY_SCENE ? PLAY_MAP : PLAY_SCENE;
		return p->view == PLAY_MAP ? PL_MAP : PL_SCENE;
#ifdef DEBUG
	case KEY_UP: case KEY_DOWN:
		play_spot(p, (uint8_t)(p->spot + (key == KEY_DOWN ? 1 : PLAY_SPOTS - 1)));
		p->view = PLAY_SCENE;
		return PL_RENDER;
#endif
	}
	return 0;
}

int play_tap(Play *p, int16_t x, int16_t y)
{
	(void)x;
	if (y >= 160 - RENDER_STATUS) return PL_BACK;
	return play_key(p, KEY_SELECT);
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

/* Appends s to t at *n. */
static void SEC_UI add(char *t, int16_t *n, const char *s)
{
	while (*s) t[(*n)++] = *s++;
	t[*n] = 0;
}

void play_status(Surface *s, const Play *p, const char *info)
{
	const Hole *h = p->rv.hole;
	char t[24];
	int16_t n = 0, y = 160 - RENDER_STATUS;

	ui_fill(s, 0, y, 160, RENDER_STATUS, UIC_BLACK);
	add(t, &n, "Hole ");
	n += play_num(t + n, h->number);
	add(t, &n, " Par ");
	n += play_num(t + n, h->par);
	add(t, &n, " ");
	n += play_num(t + n, h->tees[0].yards);
	add(t, &n, " yd");
	ui_text(s, 0, y, t, n, UIC_WHITE, -1);
	if (!info) info = h->name;
	for (n = 0; info[n] && n < 20; n++) {}
	ui_text(s, 0, y + UI_CHAR, info, n, UIC_WHITE, -1);
}

void play_map(Surface *s, const Play *p)
{
	RenderMap m;
	char t[12];
	int16_t n, x, y, sc = s->scale;

	if (render_map(s, &p->rv, &m) != 0) return;
	n = play_num(t, m.yards);
	t[n++] = 'y';
	t[n] = 0;
	/* the yardage in a black box beside the middle of the line, kept on screen */
	x = (int16_t)((m.bx + m.px) / 2 / sc + 4);
	y = (int16_t)((m.by + m.py) / 2 / sc - UI_CHAR / 2);
	if (x + n * UI_CHAR > 159) x = (int16_t)((m.bx + m.px) / 2 / sc - 4 - n * UI_CHAR);
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (y > 160 - RENDER_STATUS - UI_CHAR) y = 160 - RENDER_STATUS - UI_CHAR;
	ui_fill(s, x - 1, y - 1, n * UI_CHAR + 2, UI_CHAR + 2, UIC_BLACK);
	ui_text(s, x, y, t, n, UIC_WHITE, -1);
}
