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

static void surface(Surface *s, uint8_t scale)
{
	memset(s, 0, sizeof *s);
	s->scale = scale;
	s->depth = scale > 1 ? 8 : 2;
	s->w = s->h = 160 * scale;
	s->rowBytes = (uint16_t)((s->w * s->depth + 15) / 16 * 2);
	s->bits = calloc(s->rowBytes, s->h);
	gfx_init(s);
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
		snprintf(name, sizeof name, "Pic%d-%d.bin", s->depth, id + k);
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

	snprintf(path, sizeof path, "%s/%s-%d.ppm", outdir, name, s->w);
	f = fopen(path, "wb");
	if (!f) { CHECK(!"open ppm"); return; }
	fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
	for (y = 0; y < s->h; y++)
		for (x = 0; x < s->w; x++) {
			const uint8_t *row = s->bits + (uint32_t)y * s->rowBytes;
			if (s->depth == 8) {
				fwrite(pal + row[x] * 3, 1, 3, f);
			} else {
				uint8_t v = (row[x >> 2] >> (6 - 2 * (x & 3))) & 3, g = (uint8_t)(255 - v * 85);
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
	free(s.bits);
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
	free(s.bits);
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
	input();
	bad_bands();
	printf("uitest: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
