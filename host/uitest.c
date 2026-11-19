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
	uint16_t id = front_picture(f), k;
	int bands = 0;

	gfx_fill(s, 0, 0, s->w, s->h, UIC_BLACK);
	for (k = 0; ; k++) {
		long len;
		uint8_t *d;
		/* the 2-bit pictures below 8 bits, the 8-bit ones above, as main.c */
		snprintf(name, sizeof name, "Pic%d-%d.bin", s->depth >= 8 ? 8 : 2, id + k);
		if (!(d = load(name, &len))) break;
		CHECK(gfx_pic_band(s, d, (uint32_t)len) == 0);
		free(d);
		bands++;
	}
	CHECK(bands > 0);
	front_draw(f, s);
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
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.dialog);
	draw(&s, &f);
	save(&s, "dialog");
	sfree(&s);
}

/* The title, menu and a dialog on another screen: in a 160 unit square in
 * the middle, with the picture made for the nearest depth. Saved as
 * NAME-title and so on. */
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
	CHECK(front_key(&f, KEY_SELECT) == FR_REDRAW && f.dialog);
	draw(&s, &f);
	snprintf(t, sizeof t, "%s-dialog", name);
	save(&s, t);
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
		uint16_t id = front_picture(&f), i;
		surface_wh(&a, 160, 160, 1, depth);
		surface_wh(&b, 320, 320, 2, depth);
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
	CHECK(f.menu.count == 5);
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
	CHECK(ui_menu_hit(&f.menu, f.menu.x + 20, f.menu.y + 5 * f.menu.lineH + 2) == -1);
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

/* The courses screen: lists the installed courses and Back, picks one, and
 * goes back to the clubhouse on Courses. Drawn to host-courses-*.png. */
static void courses(uint8_t scale)
{
	static const char *const names[2] = { "Auld Kirk", "Test" };
	Surface s;
	Front f;

	surface(&s, scale);
	front_init(&f);
	front_courses(&f, names, 2, 0);
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
	pic_double(1);
	pic_double(2);
	pic_double(4);
	pic_double(8);
	input();
	courses(1);
	courses(2);
	bad_bands();
	text_paths();
	printf("uitest: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
