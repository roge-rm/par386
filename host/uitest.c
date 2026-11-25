/* Host test for the UI kit and the front screens. Draws each screen from the
 * packed pictures at 160x160 2-bit and 320x320 8-bit, as the Palm build does,
 * writes them as PPMs and checks keys and taps move through the screens.
 * Usage: uitest ARTDIR OUTDIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "front.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char *artdir, *outdir;
static uint8_t pal[768];

static uint8_t *load(const char *name, long *len)
{
	char path[512];
	FILE *f;
	uint8_t *d;

	snprintf(path, sizeof path, "%s/%s", artdir, name);
	f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	*len = ftell(f);
	fseek(f, 0, SEEK_SET);
	d = malloc(*len);
	if (fread(d, 1, *len, f) != (size_t)*len) { free(d); d = NULL; }
	fclose(f);
	return d;
}

#define GUARD 64

static void surface_wh(Surface *s, int w, int h, uint8_t scale, uint8_t depth)
{
	uint8_t *b;
	memset(s, 0, sizeof *s);
	s->scale = scale;
	s->depth = depth;
	s->w = (int16_t)w;
	s->h = (int16_t)h;
	s->rowBytes = (uint16_t)((s->w * s->depth + 15) / 16 * 2);
	b = malloc((size_t)s->rowBytes * s->h + 2 * GUARD);
	memset(b, 0xA5, (size_t)s->rowBytes * s->h + 2 * GUARD);
	memset(b + GUARD, 0, (size_t)s->rowBytes * s->h);
	s->bits = b + GUARD;
	gfx_init(s);
}

static void surface(Surface *s, uint8_t scale) { surface_wh(s, 160 * scale, 160 * scale, scale, scale > 1 ? 8 : 2); }

/* Checks the guard bytes either side and frees the surface. */
static void sfree(Surface *s)
{
	uint8_t *b = s->bits - GUARD, *e = s->bits + (size_t)s->rowBytes * s->h;
	int i, bad = 0;
	for (i = 0; i < GUARD; i++) bad += (b[i] != 0xA5) + (e[i] != 0xA5);
	CHECK(bad == 0);
	free(b);
}

/* The platform's part of a redraw: the picture's bands, then the front. */
static void draw(Surface *s, const Front *f)
{
	char name[64];
	uint16_t id = front_picture(f, s), k;
	int bands = 0;

	gfx_fill(s, 0, 0, s->w, s->h, UIC_BLACK);
	for (k = 0; ; k++) {
		long len;
		uint8_t *d;
		/* the 2-bit pictures below 8 bits, the 8-bit ones above, as main.c */
		snprintf(name, sizeof name, "Pic%d-%d.bin", s->depth >= 8 ? 8 : 2, id + k);
		if (!(d = load(name, &len))) break;
		CHECK(front_picture_band(s, id, d, (uint32_t)len) == 0);
		free(d);
		bands++;
	}
	CHECK(bands > 0);
	front_draw(f, s);
	if ((id = front_preview(f)) != 0) {
		for (k = 0; ; k++) {
			long len;
			uint8_t *d;
			snprintf(name, sizeof name, "Pic%d-%d.bin", s->depth >= 8 ? 8 : 2, id + k);
			if (!(d = load(name, &len))) break;
			CHECK(front_preview_band(s, d, (uint32_t)len) == 0);
			free(d);
		}
		CHECK(k > 0);
	}
}

static void save(const Surface *s, const char *name)
{
	char path[512];
	FILE *f;
	int16_t x, y;

	if (s->w == s->h) snprintf(path, sizeof path, "%s/%s-%d.ppm", outdir, name, s->w);
	else snprintf(path, sizeof path, "%s/%s-%dx%d.ppm", outdir, name, s->w, s->h);
	f = fopen(path, "wb");
	if (!f) { CHECK(!"open ppm"); return; }
	fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
	for (y = 0; y < s->h; y++)
		for (x = 0; x < s->w; x++) {
			const uint8_t *row = s->bits + (uint32_t)y * s->rowBytes;
			if (s->depth == 8) {
				fwrite(pal + row[x] * 3, 1, 3, f);
			} else {
				int d = s->depth, per = 8 / d, top = (1 << d) - 1;
				uint8_t v = (uint8_t)((row[x / per] >> (8 - d - d * (x % per))) & top), g = (uint8_t)(255 - v * 255 / top);
				fputc(g, f); fputc(g, f); fputc(g, f);
			}
		}
	fclose(f);
}

static void screens(uint8_t scale)
{
	Surface s;
	Front f;

	surface(&s, scale);
	front_init(&f);
	draw(&s, &f);
	save(&s, "title");
	CHECK(front_key(&f, KEY_OTHER) == FR_REDRAW && f.screen == SCR_MENU);
	draw(&s, &f);
	save(&s, "menu");
	CHECK((front_key(&f, KEY_DOWN) & FR_REDRAW) && f.menu.sel == 1);
	CHECK((front_key(&f, KEY_DOWN) & FR_REDRAW) && f.menu.sel == MAIN_COURSES);
	/* no courses: a message */
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.dialog);
	draw(&s, &f);
	save(&s, "dialog");
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && !f.dialog);
	f.menu.sel = MAIN_PLAYER;
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_PLAYER);
	draw(&s, &f);
	save(&s, "player");
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_PLAYER_NAME);
	draw(&s, &f);
	save(&s, "player-name");
	CHECK(front_key(&f, KEY_BACK) == FR_REDRAW && f.screen == SCR_PLAYER);
	CHECK(front_tap(&f, 80, 54) == FR_REDRAW && f.screen == SCR_CHARACTER);
	draw(&s, &f);
	save(&s, "character-editor");
	sfree(&s);
}

/* The title, menu and a dialog at other sizes and depths. */
static void screens_at(int w, int h, uint8_t scale, uint8_t depth, const char *name)
{
	Surface s;
	Front f;
	char t[64];

	surface_wh(&s, w, h, scale, depth);
	gfx_centre(&s, 1);
	front_init(&f);
	draw(&s, &f);
	snprintf(t, sizeof t, "%s-title", name);
	save(&s, t);
	CHECK(front_key(&f, KEY_OTHER) == FR_REDRAW && f.screen == SCR_MENU);
	draw(&s, &f);
	snprintf(t, sizeof t, "%s-menu", name);
	save(&s, t);
	CHECK((front_key(&f, KEY_DOWN) & FR_REDRAW) && f.menu.sel == 1);
	CHECK((front_key(&f, KEY_DOWN) & FR_REDRAW) && f.menu.sel == MAIN_COURSES);
	/* no courses: a message */
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.dialog);
	draw(&s, &f);
	snprintf(t, sizeof t, "%s-dialog", name);
	save(&s, t);
	sfree(&s);
}

static void dana_front(void)
{
	static const char *const names[] = { "Auld Kirk", "Test" };
	static const uint8_t scenery[] = { 1, 0 };
	Surface s;
	Front f;

	surface_wh(&s, 560, 160, 1, 4);
	gfx_centre(&s, 1);
	front_init(&f);
	front_dana(&f, 1);
	front_courses(&f, names, scenery, 2, 0);
	CHECK(front_dana_tap(&f, 280, 75) == FR_REDRAW && f.screen == SCR_MENU);
	CHECK(front_dana_tap(&f, 120, 90) == FR_REDRAW && f.screen == SCR_SETTINGS && f.menu.count == 5);
	CHECK(front_dana_tap(&f, 280, 74) == (FR_REDRAW | FR_GIMMES));
	CHECK(front_dana_tap(&f, 280, 90) == (FR_REDRAW | FR_LAYOUT) && f.left_layout);
	CHECK(front_dana_tap(&f, 280, 90) == (FR_REDRAW | FR_LAYOUT) && !f.left_layout);
	CHECK(front_key(&f, KEY_BACK) == FR_REDRAW && f.screen == SCR_MENU);
	CHECK(front_dana_tap(&f, 120, 74) == FR_REDRAW && f.screen == SCR_COURSES);
	draw(&s, &f);
	save(&s, "dana-courses");
	CHECK(front_dana_tap(&f, 120, 58) == FR_REDRAW && f.screen == SCR_MENU && f.course == 1);
	CHECK(front_dana_tap(&f, 120, 42) == FR_REDRAW && f.screen == SCR_GAMES);
	CHECK(front_dana_tap(&f, 180, 42) == FR_REDRAW && f.screen == SCR_PLAYERS);
	draw(&s, &f);
	save(&s, "dana-players");
	CHECK(front_dana_tap(&f, 400, 58) == (FR_PLAY | FR_REDRAW));
	front_clubhouse(&f, 0);
	f.menu.sel = MAIN_PLAYER;
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_PLAYER);
	CHECK(front_dana_tap(&f, 280, 54) == FR_REDRAW && f.screen == SCR_CHARACTER);
	CHECK(front_dana_tap(&f, 270, 73) == (FR_REDRAW | FR_CHARACTER | FR_CHARACTER_POSE) &&
	      f.character.shoes == OUTFIT_NAVY);
	draw(&s, &f);
	save(&s, "dana-character-editor");
	CHECK(front_dana_tap(&f, 313, 137) == FR_REDRAW && f.screen == SCR_PLAYER);
	CHECK(f.player.shoes == OUTFIT_DEFAULT);
	sfree(&s);
}

/* The pictures alone at scale 1 and 2 at one depth: every pixel at 2 is
 * the one at 1 under it, so 2-bit pictures are doubled. */
static void pic_double(uint8_t depth)
{
	Surface a, b;
	Front f;
	int x, y, k, bad = 0;

	front_init(&f);
	for (k = 0; k < 2; k++) {
		char name[64];
		uint16_t id, i;
		surface_wh(&a, 160, 160, 1, depth);
		surface_wh(&b, 320, 320, 2, depth);
		id = front_picture(&f, &a);
		for (i = 0; ; i++) {
			long len;
			uint8_t *d;
			snprintf(name, sizeof name, "Pic2-%d.bin", id + i);
			if (!(d = load(name, &len))) break;
			CHECK(gfx_pic_band(&a, d, (uint32_t)len) == 0);
			CHECK(gfx_pic_band(&b, d, (uint32_t)len) == 0);
			free(d);
		}
		CHECK(i > 0);
		for (y = 0; y < 320; y++)
			for (x = 0; x < 320; x++) {
				int per = 8 / depth, sh = 8 - depth - depth * ((x / 2) % per), sh2 = 8 - depth - depth * (x % per);
				const uint8_t *ra = a.bits + (y / 2) * a.rowBytes, *rb = b.bits + y * b.rowBytes;
				uint8_t va = depth == 8 ? ra[x / 2] : (uint8_t)(ra[x / 2 / per] >> sh);
				uint8_t vb = depth == 8 ? rb[x] : (uint8_t)(rb[x / per] >> sh2);
				bad += ((va ^ vb) & ((1 << depth) - 1)) != 0;
			}
		CHECK(bad == 0);
		sfree(&a);
		sfree(&b);
		front_key(&f, KEY_OTHER);
	}
}

static void input(void)
{
	Front f;
	int16_t i, x, y;

	front_init(&f);
	CHECK(front_tap(&f, 5, 5) == FR_REDRAW && f.screen == SCR_MENU);
	CHECK(f.menu.count == 6);
	/* Up from the top wraps to Quit, and selecting it quits. */
	CHECK((front_key(&f, KEY_UP) & FR_REDRAW) && f.menu.sel == MAIN_QUIT);
	CHECK(front_key(&f, KEY_SELECT) == FR_QUIT);
	CHECK(front_key(&f, KEY_DOWN) == (FR_REDRAW | FR_LINES) && f.menu.sel == 0 && f.oldSel == MAIN_QUIT);
	/* Taps on each line's text hit that line; between the bar and the board's edge they miss. */
	for (i = 0; i < f.menu.count; i++) {
		y = f.menu.y + i * f.menu.lineH;
		CHECK(ui_menu_hit(&f.menu, f.menu.x + 20, y) == i);
		CHECK(ui_menu_hit(&f.menu, f.menu.x + 20, y + 7) == i);
	}
	CHECK(ui_menu_hit(&f.menu, f.menu.x - 1, f.menu.y) == -1);
	CHECK(ui_menu_hit(&f.menu, f.menu.x + 20, f.menu.y + f.menu.count * f.menu.lineH + 2) == -1);
	CHECK(ui_menu_hit(&f.menu, f.menu.x + 20, f.menu.y - 8) == -1);
	/* A tap on Courses opens its dialog, a tap outside the button does nothing, OK closes it. */
	y = f.menu.y + MAIN_COURSES * f.menu.lineH + 3;
	CHECK(front_tap(&f, 80, y) == FR_REDRAW && f.dialog && f.menu.sel == MAIN_COURSES);
	CHECK(f.dlg.box.x >= 0 && f.dlg.box.y >= 0 && f.dlg.box.x + f.dlg.box.w + 3 <= 160 && f.dlg.box.y + f.dlg.box.h + 3 <= 160);
	CHECK(f.dlg.count == 1);
	CHECK(front_tap(&f, f.dlg.box.x + 2, f.dlg.box.y + 2) == 0 && f.dialog);
	x = f.dlg.btn[0].x + f.dlg.btn[0].w / 2;
	y = f.dlg.btn[0].y + f.dlg.btn[0].h / 2;
	CHECK(front_tap(&f, x, y) == FR_REDRAW && !f.dialog);
	/* Keys in a dialog: select closes it, the menu keeps its line. */
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.dialog);
	CHECK(front_key(&f, KEY_DOWN) == 0 && f.dialog);
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && !f.dialog && f.menu.sel == MAIN_COURSES);
	/* Tapping Quit quits. */
	CHECK(front_tap(&f, 80, f.menu.y + MAIN_QUIT * f.menu.lineH) & FR_QUIT);
	/* Lists. */
	CHECK(ui_parts("a|bb|ccc", '|') == 3);
	{
		int16_t n;
		const char *p = ui_part("a|bb|ccc", '|', 2, &n);
		CHECK(n == 3 && p[0] == 'c');
		p = ui_part("a|bb|ccc", '|', 1, &n);
		CHECK(n == 2 && p[0] == 'b');
	}
}

static void player_input(void)
{
	Front f;
	int i;
	const int change = FR_REDRAW | FR_CHARACTER | FR_CHARACTER_POSE;

	front_init(&f);
	CHECK(strcmp(f.player.name, PLAYER_DEFAULT_NAME) == 0);
	CHECK(f.player.golfer == GOLFER_MALE && f.player.skin == SKIN_LIGHT && !f.player.left);
	front_clubhouse(&f, 1);
	CHECK(f.menu.count == 7 && front_action(&f, 0) == MAIN_CONTINUE);
	CHECK(front_action(&f, MAIN_PLAYER + 1) == MAIN_PLAYER);
	f.menu.sel = MAIN_PLAYER + 1;
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_PLAYER);
	CHECK(f.menu.count == 3);
	CHECK(front_tap(&f, 30, 54) == FR_REDRAW && f.screen == SCR_CHARACTER);
	CHECK(front_tap(&f, 70, 31) == change && f.character.golfer == GOLFER_FEMALE);
	CHECK(front_key(&f, KEY_DOWN) == (FR_REDRAW | FR_CHARACTER));
	CHECK(front_key(&f, KEY_SELECT) == change && f.character.left);
	CHECK(front_tap(&f, 70, 59) == change && f.character.skin == SKIN_MEDIUM);
	CHECK(front_key(&f, KEY_LEFT) == change && f.character.skin == SKIN_LIGHT);
	CHECK(front_key(&f, KEY_LEFT) == change && f.character.skin == SKIN_DARK);
	CHECK(front_tap(&f, 70, 73) == change && f.character.shoes == OUTFIT_NAVY);
	CHECK(front_tap(&f, 70, 87) == change && f.character.pants == OUTFIT_NAVY);
	CHECK(front_key(&f, KEY_RIGHT) == change && f.character.pants == OUTFIT_GREEN);
	CHECK(front_tap(&f, 70, 101) == change && f.character.shirt == OUTFIT_NAVY);
	CHECK(front_tap(&f, 70, 115) == change && f.character.hat == OUTFIT_NAVY);
	CHECK(front_tap(&f, 133, 73) == 0);
	CHECK(f.player.golfer == GOLFER_MALE && f.player.skin == SKIN_LIGHT && !f.player.left);
	CHECK(f.player.shoes == OUTFIT_DEFAULT && f.player.pants == OUTFIT_DEFAULT &&
	      f.player.shirt == OUTFIT_DEFAULT && f.player.hat == OUTFIT_DEFAULT);
	CHECK(front_tap(&f, 47, 137) == (FR_REDRAW | FR_PLAYER) && f.screen == SCR_PLAYER);
	CHECK(f.player.golfer == GOLFER_FEMALE && f.player.skin == SKIN_DARK && f.player.left);
	CHECK(f.player.shoes == OUTFIT_NAVY && f.player.pants == OUTFIT_GREEN &&
	      f.player.shirt == OUTFIT_NAVY && f.player.hat == OUTFIT_NAVY);
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_CHARACTER);
	CHECK(front_key(&f, KEY_SELECT) == change && f.character.golfer == GOLFER_MALE);
	CHECK(front_key(&f, KEY_BACK) == FR_REDRAW && f.screen == SCR_PLAYER && f.player.golfer == GOLFER_FEMALE);
	CHECK(front_tap(&f, 30, 36) == FR_REDRAW && f.screen == SCR_PLAYER_NAME);
	CHECK(front_key(&f, KEY_SELECT) == (FR_REDRAW | FR_NAME) && f.player_name[0] == 0);
	CHECK(front_tap(&f, 24, 51) == (FR_REDRAW | FR_NAME) && strcmp(f.player_name, "A") == 0);
	CHECK(front_key(&f, KEY_DOWN) == (FR_REDRAW | FR_NAME));
	CHECK(front_key(&f, KEY_SELECT) == (FR_REDRAW | FR_NAME) && strcmp(f.player_name, "AG") == 0);
	CHECK(front_tap(&f, 65, 125) == (FR_REDRAW | FR_NAME) && strcmp(f.player_name, "A") == 0);
	CHECK(front_tap(&f, 133, 125) == (FR_REDRAW | FR_NAME));
	CHECK(front_tap(&f, 46, 51) == (FR_REDRAW | FR_NAME) && strcmp(f.player_name, "Ab") == 0);
	CHECK(front_tap(&f, 133, 125) == (FR_REDRAW | FR_NAME));
	CHECK(front_tap(&f, 68, 51) == (FR_REDRAW | FR_NAME) && strcmp(f.player_name, "AbC") == 0);
	CHECK(front_tap(&f, 30, 139) == (FR_REDRAW | FR_PLAYER) && strcmp(f.player.name, "AbC") == 0);
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_PLAYER_NAME);
	for (i = 0; i < 10; i++) CHECK(front_text(&f, 'a' + i) == (FR_REDRAW | FR_NAME));
	CHECK(strcmp(f.player_name, "AbCabcde") == 0);
	CHECK(front_text(&f, '|') == 0);
	CHECK(front_text(&f, 27) == FR_REDRAW && strcmp(f.player.name, "AbC") == 0);
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW);
	CHECK(front_text(&f, '\b') == (FR_REDRAW | FR_NAME) && strcmp(f.player_name, "Ab") == 0);
	CHECK(front_tap(&f, 103, 125) == (FR_REDRAW | FR_NAME));
	CHECK(front_text(&f, ' ') == (FR_REDRAW | FR_NAME));
	CHECK(front_text(&f, '\r') == (FR_REDRAW | FR_PLAYER) && strcmp(f.player.name, PLAYER_DEFAULT_NAME) == 0);
	CHECK(front_key(&f, KEY_BACK) == FR_REDRAW && f.screen == SCR_MENU && f.menu.sel == MAIN_PLAYER + 1);
}

static void name_updates(int w, int h, uint8_t scale, uint8_t depth)
{
	Surface s, shown, full;
	Front f;
	uint8_t wide = w == 560;
	int step;

	surface_wh(&s, w, h, scale, depth);
	surface_wh(&shown, w, h, scale, depth);
	surface_wh(&full, w, h, scale, depth);
	gfx_centre(&s, 1);
	gfx_centre(&full, 1);
	front_init(&f);
	front_clubhouse(&f, 0);
	f.menu.sel = MAIN_PLAYER;
	front_key(&f, KEY_SELECT);
	front_key(&f, KEY_SELECT);
	front_draw(&f, &s);
	gfx_copy(&shown, &s, 0, 0, w, h);

	for (step = 0; step < 4; step++) {
		static const int16_t x[] = { 103, 133, 24, 24 };
		static const int16_t y[] = { 125, 125, 51, 51 };
		static const int16_t wide_x[] = { 330, 400, 150, 150 };
		static const uint8_t counts[] = { 1, 2, 3, 1 };
		uint8_t old_key = f.name_key, old_lower = f.name_lower, i, n;
		URect dirty[3];
		int r = wide ? front_dana_tap(&f, wide_x[step], y[step]) : front_tap(&f, x[step], y[step]);

		CHECK(r == (FR_REDRAW | FR_NAME));
		n = front_name_update(&f, &s, old_key, old_lower, dirty);
		CHECK(n == counts[step]);
		for (i = 0; i < n; i++)
			gfx_copy(&shown, &s, dirty[i].x, dirty[i].y, dirty[i].w, dirty[i].h);
		front_draw(&f, &full);
		CHECK(memcmp(shown.bits, full.bits, (size_t)s.rowBytes * s.h) == 0);
	}
	CHECK(strcmp(f.player_name, "aa") == 0);
	sfree(&s);
	sfree(&shown);
	sfree(&full);
}

/* The updates must copy enough for the display to match a full redraw. */
static void character_updates(int w, int h, uint8_t scale, uint8_t depth)
{
	static const uint8_t counts[] = { 2, 2, 3 };
	Surface s, shown, full;
	Front f;
	uint8_t step;

	surface_wh(&s, w, h, scale, depth);
	surface_wh(&shown, w, h, scale, depth);
	surface_wh(&full, w, h, scale, depth);
	gfx_centre(&s, 1);
	gfx_centre(&full, 1);
	front_init(&f);
	front_clubhouse(&f, 0);
	f.menu.sel = MAIN_PLAYER;
	front_key(&f, KEY_SELECT);
	front_tap(&f, 30, 54);
	front_draw(&f, &s);
	gfx_copy(&shown, &s, 0, 0, w, h);
	for (step = 0; step < sizeof counts; step++) {
		uint8_t old = f.character_row, n, i;
		URect dirty[3];
		int r = step == 0 ? front_key(&f, KEY_DOWN) : step == 1 ? front_key(&f, KEY_RIGHT) :
		                    front_tap(&f, 70, 115);

		CHECK(r == (FR_REDRAW | FR_CHARACTER | (step ? FR_CHARACTER_POSE : 0)));
		n = front_character_update(&f, &s, old, (r & FR_CHARACTER_POSE) != 0, dirty);
		CHECK(n == counts[step]);
		for (i = 0; i < n; i++)
			gfx_copy(&shown, &s, dirty[i].x, dirty[i].y, dirty[i].w, dirty[i].h);
		front_draw(&f, &full);
		CHECK(memcmp(shown.bits, full.bits, (size_t)s.rowBytes * s.h) == 0);
	}
	sfree(&s);
	sfree(&shown);
	sfree(&full);
}

/* The courses screen: lists the installed courses and Back, picks one, and
 * goes back to the clubhouse on Courses. Drawn to host-courses-*.png. */
static void courses(uint8_t scale)
{
	static const char *const names[2] = { "Auld Kirk", "Test" };
	static const uint8_t scenery[2] = { 1, 0 };
	Surface s;
	Front f;

	surface(&s, scale);
	front_init(&f);
	front_courses(&f, names, scenery, 2, 0);
	front_clubhouse(&f, 0);
	f.menu.sel = MAIN_COURSES;
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_COURSES && !f.dialog);
	CHECK(f.menu.count == 3 && f.menu.sel == 0);
	draw(&s, &f);
	save(&s, "courses");
	CHECK(front_key(&f, KEY_DOWN) & FR_REDRAW);
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_MENU && f.course == 1);
	CHECK(f.menu.sel == MAIN_COURSES);
	/* Back keeps the pick */
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_COURSES && f.menu.sel == 1);
	CHECK(front_key(&f, KEY_BACK) == FR_REDRAW && f.screen == SCR_MENU && f.course == 1);
	/* with a saved round the menu has Continue first */
	front_clubhouse(&f, 1);
	f.menu.sel = MAIN_COURSES + 1;
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.screen == SCR_COURSES);
	CHECK(front_tap(&f, 80, f.menu.y + 3) & FR_REDRAW);
	CHECK(f.screen == SCR_MENU && f.course == 0 && f.menu.sel == MAIN_COURSES + 1 && f.saved);
	sfree(&s);
}

/* A band that runs past the surface or is cut short is refused. */
static void bad_bands(void)
{
	Surface s;
	uint8_t band[16] = { 0, 150, 0, 20, 0, 40, 2, 0, 0x81, 0 };

	surface(&s, 1);
	CHECK(gfx_pic_band(&s, band, 10) == -1);           /* rows 150-169 */
	band[1] = 0;
	CHECK(gfx_pic_band(&s, band, 10) == -1);           /* 127 bytes of 800 */
	band[6] = 8;
	CHECK(gfx_pic_band(&s, band, 10) == -1);           /* wrong depth */
	sfree(&s);
}

/* A wide band cropped on all four sides, including an odd surface stride. */
static void cropped_band(void)
{
	static const uint8_t band[] = {
		0, 0, 0, 4, 0, 5, 8, 0, 19,
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
		11, 12, 13, 14, 15, 16, 17, 18, 19, 20
	};
	uint8_t truncated[sizeof band];
	Surface s;

	surface_wh(&s, 3, 2, 2, 8);
	s.ox = s.oy = -1;
	CHECK(gfx_pic_band_crop8(&s, band, sizeof band) == 0);
	CHECK(!memcmp(s.bits, "\x07\x08\x09\x00\x0c\x0d\x0e\x00", 8));
	sfree(&s);

	surface_wh(&s, 5, 2, 2, 8);
	memcpy(truncated, band, sizeof band);
	truncated[1] = 1;
	truncated[2] = truncated[3] = 255;
	CHECK(gfx_pic_band_crop8(&s, truncated, sizeof truncated) == -1);
	CHECK(!memcmp(s.bits + s.rowBytes, "\x01\x02\x03\x04\x05\x00", 6));
	sfree(&s);
}

/* The byte-at-a-time 2-bit text matches the pixel-at-a-time path: the same
 * text at every offset in a byte, once on a full surface (fast path) and
 * once on one cut 1 pixel short of the text's end (pixel path). */
static void text_paths(void)
{
	Surface a, b;
	uint8_t ba[40 * 16], bb[40 * 16];
	int x, y, i;

	static const int xs[8] = {0, 1, 2, 3, 149, 150, 151, 152};
	int xi;

	for (xi = 0; xi < 8; xi++) {
		x = xs[xi];
		memset(&a, 0, sizeof a);
		a.depth = 2;
		a.scale = 1;
		a.w = 160;
		a.h = 16;
		a.rowBytes = 40;
		a.bits = ba;
		gfx_init(&a);
		b = a;
		b.bits = bb;
		b.w = (int16_t)(x + 7);
		memset(ba, 0x55, sizeof ba);
		memset(bb, 0x55, sizeof bb);
		for (i = 0; i < UIC_COUNT; i += 3) {
			gfx_text(&a, (int16_t)x, (int16_t)(i & 7), "W", 1, (uint8_t)i);
			gfx_text(&b, (int16_t)x, (int16_t)(i & 7), "W", 1, (uint8_t)i);
		}
		/* compare the pixels b could draw */
		for (y = 0; y < 16; y++)
			for (i = 0; i < x + 7; i++) {
				int sh = 6 - 2 * (i & 3);
				CHECK(((ba[y * 40 + i / 4] >> sh) & 3) == ((bb[y * 40 + i / 4] >> sh) & 3));
			}
	}
}

/* The fade's swaps: after the platform's steps (14 of the 16 dither values,
 * then 2 at a time) both surfaces are as they were, and half way the first
 * shows about half of each in the rows asked for. At every depth, and with rows
 * of an odd number of words. */
/* gfx_to16_mask: the steps 0-5, 5-10, 10-16 together fill what gfx_to16 does, and step 0-5 alone only some of it */
static void to16s(int w, uint8_t le)
{
	Surface a;
	uint16_t rb = (uint16_t)(w * 2 + 4);
	uint8_t *d1 = calloc(rb, 40), *d2 = calloc(rb, 40);
	int lo, i, part = 0;

	surface_wh(&a, w, 40, 1, 8);
	for (i = 0; i < a.rowBytes * a.h; i++) a.bits[i] = (uint8_t)(i * 37 + 11);
	gfx_to16(&a, d1, rb, 0, 0, w, 40, le);
	gfx_to16_mask(&a, d2, rb, 0, 0, (int16_t)w, 40, le, 0, 5);
	for (i = 0; i < rb * 40; i++) part += d1[i] == d2[i] && d1[i];
	CHECK(part > 0 && memcmp(d1, d2, (size_t)rb * 40) != 0);
	for (lo = 5; lo < 16; lo += 5) gfx_to16_mask(&a, d2, rb, 0, 0, (int16_t)w, 40, le, (uint8_t)lo, (uint8_t)(lo + 5 > 16 ? 16 : lo + 5));
	CHECK(memcmp(d1, d2, (size_t)rb * 40) == 0);
	sfree(&a);
	free(d1);
	free(d2);
}

/* gfx_copy_mask: three steps fill all of a rectangle (rounded out to 4 byte groups) and nothing past it */
static uint8_t px_at(const Surface *s, int x, int y)
{
	uint8_t b = s->bits[y * s->rowBytes + ((x * s->depth) >> 3)];
	int sh = 8 - s->depth - ((x * s->depth) & 7);
	return (uint8_t)((b >> sh) & ((1 << s->depth) - 1));
}

static void copies(int w, uint8_t depth)
{
	Surface a, b;
	int lo, x, y, bad = 0, got = 0;
	uint32_t i, n;
	int16_t px = (int16_t)(32 / depth), x0 = (int16_t)(px + 1), x1 = (int16_t)(w - px - 3);

	surface_wh(&a, w, 40, 1, depth);
	surface_wh(&b, w, 40, 1, depth);
	n = (uint32_t)a.rowBytes * a.h;
	for (i = 0; i < n; i++) b.bits[i] = (uint8_t)(i * 91 + 5);
	gfx_copy_mask(&a, &b, x0, 3, x1, 30, 0, 5);
	for (y = 0; y < 40; y++)
		for (x = 0; x < w; x++) got += px_at(&a, x, y) != 0;
	CHECK(got > 0);
	for (lo = 5; lo < 16; lo += 5) gfx_copy_mask(&a, &b, x0, 3, x1, 30, (uint8_t)lo, (uint8_t)(lo + 5 > 16 ? 16 : lo + 5));
	for (y = 0; y < 40; y++)
		for (x = 0; x < w; x++) {
			uint8_t pa = px_at(&a, x, y), pb = px_at(&b, x, y);
			if (y >= 3 && y < 30 && x >= x0 && x < x1) bad += pa != pb;
			else if (y < 3 || y >= 30 || x < px || x >= w - px) bad += pa != 0;
		}
	CHECK(bad == 0);
	sfree(&a);
	sfree(&b);
}

static void swaps(int w, uint8_t depth)
{
	Surface a, b, a0, b0;
	int x, y, lo, same = 0, total = 0;
	uint32_t i, n;

	surface_wh(&a, w, 40, 1, depth);
	surface_wh(&b, w, 40, 1, depth);
	surface_wh(&a0, w, 40, 1, depth);
	surface_wh(&b0, w, 40, 1, depth);
	n = (uint32_t)a.rowBytes * a.h;
	for (i = 0; i < n; i++) {
		a.bits[i] = (uint8_t)(i * 37 + 11);
		b.bits[i] = (uint8_t)(i * 91 + 5);
	}
	memcpy(a0.bits, a.bits, n);
	memcpy(b0.bits, b.bits, n);
	gfx_swap_mask(&a, &b, 2, 38, 2, 16);
	for (lo = 2; lo < 16; lo += 2) {
		if (lo == 8) {
			/* half way: about half the pixels of rows 2 to 37 are a's own, the rest b's */
			uint8_t m = (uint8_t)((1 << depth) - 1);
			for (y = 2; y < 38; y++)
				for (x = 0; x < w; x++) {
					int bit = (x * depth) & 7, sh = 8 - depth - bit;
					uint8_t pa = (uint8_t)((a.bits[y * a.rowBytes + x * depth / 8] >> sh) & m);
					uint8_t pa0 = (uint8_t)((a0.bits[y * a.rowBytes + x * depth / 8] >> sh) & m);
					uint8_t pb0 = (uint8_t)((b0.bits[y * a.rowBytes + x * depth / 8] >> sh) & m);
					if (pa0 == pb0) continue;
					total++;
					same += pa == pa0;
					CHECK(pa == pa0 || pa == pb0);
				}
			CHECK(same * 3 > total && same * 3 < total * 2);
		}
		gfx_swap_mask(&a, &b, 2, 38, (uint8_t)lo, (uint8_t)(lo + 2));
	}
	CHECK(memcmp(a.bits, a0.bits, n) == 0);
	CHECK(memcmp(b.bits, b0.bits, n) == 0);
	sfree(&a);
	sfree(&b);
	sfree(&a0);
	sfree(&b0);
}

int main(int argc, char **argv)
{
	long len;
	uint8_t *p;

	if (argc < 3) { fprintf(stderr, "usage: uitest ARTDIR OUTDIR\n"); return 2; }
	artdir = argv[1];
	outdir = argv[2];
	mkdir(outdir, 0777);
	p = load("Pal8-1000.bin", &len);
	CHECK(p && len == 768);
	if (p) { memcpy(pal, p, 768); free(p); }
	/* UI colour n is palette entry n + 1. */
	CHECK(pal[3 * (UIC_TEXT_HI + 1)] == 255 && pal[3 * (UIC_TEXT_HI + 1) + 1] == 224);
	CHECK(gfx_ui_rgb(UIC_TEXT_HI) == 0xFFE05A);

	screens(1);
	screens(2);
	screens_at(160, 160, 1, 4, "d4");
	screens_at(160, 160, 1, 1, "d1");
	screens_at(160, 160, 1, 8, "d8");
	screens_at(320, 480, 2, 8, "tall");
	screens_at(480, 320, 2, 8, "wide");
	screens_at(320, 448, 2, 8, "tx");
	/* grey 320 screens (Sony HR, or stepped down for memory) get the 2-bit pictures doubled */
	screens_at(320, 320, 2, 4, "q4");
	screens_at(320, 320, 2, 2, "q2");
	screens_at(320, 320, 2, 1, "q1");
	screens_at(320, 480, 2, 4, "tall4");
	screens_at(480, 320, 2, 2, "wide2");
	screens_at(560, 160, 1, 4, "dana");
	dana_front();
	pic_double(1);
	pic_double(2);
	pic_double(4);
	pic_double(8);
	input();
	player_input();
	name_updates(160, 160, 1, 2);
	name_updates(320, 480, 2, 8);
	name_updates(560, 160, 1, 4);
	character_updates(160, 160, 1, 2);
	character_updates(320, 448, 2, 8);
	character_updates(560, 160, 1, 4);
	courses(1);
	courses(2);
	bad_bands();
	cropped_band();
	text_paths();
	swaps(160, 1);
	swaps(160, 2);
	swaps(160, 4);
	swaps(160, 8);
	swaps(176, 1);
	swaps(176, 8);
	swaps(48, 2);
	to16s(160, 0);
	copies(160, 1);
	copies(160, 2);
	copies(176, 4);
	copies(160, 8);
	to16s(162, 1);
	printf("uitest: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
