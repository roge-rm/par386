#include "front.h"
#include "sound.h"

/* The menu board's felt and name plate in the menu picture (tools/mkart.py). */
#define PLATE_CX 80
#define PLATE_Y 5
#define PLATE_H 13

/* The players screen's Back line. */
#define BACK_LINE 4

static void SEC_UI main_menu(Front *f)
{
	if (f->saved)
		ui_menu_init(&f->menu, "Continue round|Play a round|Practice|Courses|Settings|Quit", 16, 34, 128, 18);
	else
		ui_menu_init(&f->menu, "Play a round|Practice|Courses|Settings|Quit", 22, 36, 116, 20);
}

void front_init(Front *f)
{
	f->screen = SCR_TITLE;
	f->dialog = DLG_NONE;
	f->saved = 0;
	f->players = 1;
	f->oldSel = 0;
	f->courses = 0;
	f->course = 0;
	f->course_list[0] = 0;
	f->sound = SOUND_ON;
	main_menu(f);
}

void front_courses(Front *f, const char *const *names, const uint8_t *scenery, uint8_t count, uint8_t pick)
{
	uint8_t i, k;
	uint16_t n = 0;
	const char *b = "Back";

	if (count > FRONT_COURSES) count = FRONT_COURSES;
	for (i = 0; i < count; i++) {
		for (k = 0; names[i][k] && k < 23; k++) f->course_list[n++] = names[i][k] == '|' ? ' ' : names[i][k];
		f->course_list[n++] = '|';
		f->scenery[i] = scenery ? scenery[i] : SCENERY_UNKNOWN;
	}
	while (*b) f->course_list[n++] = *b++;
	f->course_list[n] = 0;
	f->courses = count;
	f->course = pick < count ? pick : 0;
}

void front_clubhouse(Front *f, uint8_t saved)
{
	f->screen = SCR_MENU;
	f->dialog = DLG_NONE;
	f->saved = saved;
	main_menu(f);
}

uint8_t front_action(const Front *f, uint8_t line)
{
	if (!f->saved) return line;
	return line ? (uint8_t)(line - 1) : MAIN_CONTINUE;
}

uint16_t front_picture(const Front *f)
{
	return f->screen == SCR_TITLE ? PIC_TITLE : PIC_MENU;
}

uint16_t front_preview(const Front *f)
{
	uint8_t sc;

	if (f->screen != SCR_COURSES || f->dialog || f->menu.sel >= f->courses) return 0;
	sc = f->scenery[f->menu.sel];
	return sc == SCENERY_UNKNOWN ? 0 : (uint16_t)(PIC_PREVIEW + 10 * sc);
}

int front_preview_band(Surface *s, const uint8_t *d, uint32_t len)
{
	int16_t ox = s->ox, oy = s->oy;
	int r;

	s->ox = (int16_t)(ox + PREVIEW_X * s->scale);
	s->oy = (int16_t)(oy + PREVIEW_Y * s->scale);
	r = gfx_pic_band(s, d, len);
	s->ox = ox;
	s->oy = oy;
	return r;
}

void front_draw(const Front *f, Surface *s)
{
	const char *plate = f->screen == SCR_PLAYERS ? "PLAYERS" : f->screen == SCR_COURSES ? "COURSES" :
	                    f->screen == SCR_SETTINGS ? "SETTINGS" : "CLUBHOUSE";
	int16_t n;

	if (f->screen == SCR_TITLE) {
		ui_ctext(s, 80, 146, "PRESS ANY BUTTON", 16, UIC_WHITE, UIC_BLACK);
		return;
	}
	for (n = 0; plate[n]; n++) {}
	ui_ctext(s, PLATE_CX + 1, PLATE_Y + (PLATE_H - UI_CHAR) / 2 + 1, plate, n, UIC_BRASS_LT, -1);
	ui_ctext(s, PLATE_CX, PLATE_Y + (PLATE_H - UI_CHAR) / 2, plate, n, UIC_BRASS_DK, -1);
	if (f->screen == SCR_COURSES && f->menu.sel < f->courses) {
		/* the preview's frame, and a dark box the picture goes in, if there is one */
		ui_bevel(s, PREVIEW_X - 1, PREVIEW_Y - 1, PREVIEW_W + 2, PREVIEW_H + 2, UIC_BAR_DK, UIC_BAR_LT);
		ui_fill(s, PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, UIC_BLACK);
		if (f->scenery[f->menu.sel] == SCENERY_UNKNOWN)
			ui_ctext(s, PREVIEW_X + PREVIEW_W / 2, PREVIEW_Y + (PREVIEW_H - UI_CHAR) / 2, "ON CARD", 7, UIC_TEXT, -1);
	}
	ui_menu_draw(s, &f->menu);
	if (f->dialog)
		ui_dialog_draw(s, &f->dlg);
}

static int SEC_UI players_screen(Front *f)
{
	f->screen = SCR_PLAYERS;
	ui_menu_init(&f->menu, "1 player|2 players|3 players|4 players|Back", 22, 36, 116, 20);
	f->menu.sel = (uint8_t)(f->players ? f->players - 1 : 0);
	return FR_REDRAW;
}

static int SEC_UI courses_screen(Front *f)
{
	f->screen = SCR_COURSES;
	/* tighter lines to leave room for the preview below */
	ui_menu_init(&f->menu, f->course_list, 22, 24, 116, 14);
	f->menu.sel = f->course;
	return FR_REDRAW;
}

/* The settings screen's lines: the sound setting, then Back. */
static void SEC_UI settings_menu(Front *f)
{
	const char *a = "Sound: ", *b = f->sound == SOUND_FX ? "Effects" : f->sound == SOUND_OFF ? "Off" : "On", *c = "|Back";
	uint8_t n = 0, sel = f->screen == SCR_SETTINGS ? f->menu.sel : 0;

	while (*a) f->settings_list[n++] = *a++;
	while (*b) f->settings_list[n++] = *b++;
	while (*c) f->settings_list[n++] = *c++;
	f->settings_list[n] = 0;
	f->screen = SCR_SETTINGS;
	ui_menu_init(&f->menu, f->settings_list, 16, 36, 128, 20);
	f->menu.sel = sel;
}

static int SEC_UI open_item(Front *f, uint8_t i)
{
	if (f->screen == SCR_SETTINGS) {
		if (i == 0) {
			f->sound = (uint8_t)((f->sound + 1) % SOUND_MODES);
			settings_menu(f);
			return FR_REDRAW | FR_SOUND;
		}
		front_clubhouse(f, f->saved);
		f->menu.sel = (uint8_t)(MAIN_SETTINGS + (f->saved ? 1 : 0));
		return FR_REDRAW;
	}
	if (f->screen == SCR_COURSES) {
		if (i < f->courses) f->course = i;
		front_clubhouse(f, f->saved);
		f->menu.sel = (uint8_t)(MAIN_COURSES + (f->saved ? 1 : 0));
		return FR_REDRAW;
	}
	if (f->screen == SCR_PLAYERS) {
		if (i >= BACK_LINE) {
			front_clubhouse(f, f->saved);
			f->menu.sel = f->saved;
			return FR_REDRAW;
		}
		f->players = (uint8_t)(i + 1);
		return FR_PLAY;
	}
	i = front_action(f, i);
	if (i == MAIN_QUIT) return FR_QUIT;
	if (i == MAIN_CONTINUE) return FR_CONTINUE;
	if (i == MAIN_PLAY) {
		if (!f->saved) return players_screen(f);
		ui_dialog_init(&f->dlg, "New round", "The saved round\nwill be lost.", "OK|Cancel");
		f->dialog = DLG_NEW_ROUND;
		return FR_REDRAW;
	}
	switch (i) {
	case MAIN_PRACTICE: ui_dialog_init(&f->dlg, "Practice", "The range and the\ngreen come later.", "OK"); break;
	case MAIN_COURSES:
		if (f->courses) return courses_screen(f);
		ui_dialog_init(&f->dlg, "Courses", "No courses are\ninstalled.", "OK");
		break;
	default:
		settings_menu(f);
		return FR_REDRAW;
	}
	f->dialog = DLG_MESSAGE;
	return FR_REDRAW;
}

/* Button i of the open dialog was picked. */
static int SEC_UI close_dialog(Front *f, uint8_t i)
{
	uint8_t d = f->dialog;
	f->dialog = DLG_NONE;
	if (d == DLG_NEW_ROUND && i == 0) return players_screen(f);
	return FR_REDRAW;
}

void front_message(Front *f, const char *title, const char *text)
{
	f->screen = SCR_MENU;
	ui_dialog_init(&f->dlg, title, text, "OK");
	f->dialog = DLG_MESSAGE;
}

static int SEC_UI key_in(Front *f, uint8_t key)
{
	if (key == KEY_NONE) return 0;
	if (f->screen == SCR_TITLE) {
		f->screen = SCR_MENU;
		return FR_REDRAW;
	}
	if (f->dialog) {
		switch (key) {
		case KEY_LEFT: case KEY_UP:
			if (f->dlg.count > 1) { f->dlg.sel = (uint8_t)((f->dlg.sel + f->dlg.count - 1) % f->dlg.count); return FR_REDRAW; }
			return 0;
		case KEY_RIGHT: case KEY_DOWN:
			if (f->dlg.count > 1) { f->dlg.sel = (uint8_t)((f->dlg.sel + 1) % f->dlg.count); return FR_REDRAW; }
			return 0;
		case KEY_SELECT:
			return close_dialog(f, f->dlg.sel);
		case KEY_BACK:
			return close_dialog(f, 0xFF);
		}
		return 0;
	}
	switch (key) {
	/* on the courses screen the preview changes too, so all of it is redrawn */
	case KEY_UP: f->oldSel = f->menu.sel; ui_menu_move(&f->menu, -1); return FR_REDRAW | (f->screen == SCR_COURSES ? 0 : FR_LINES);
	case KEY_DOWN: f->oldSel = f->menu.sel; ui_menu_move(&f->menu, 1); return FR_REDRAW | (f->screen == SCR_COURSES ? 0 : FR_LINES);
	case KEY_SELECT: return open_item(f, f->menu.sel);
	case KEY_BACK:
		if (f->screen == SCR_PLAYERS) return open_item(f, BACK_LINE);
		if (f->screen == SCR_COURSES) return open_item(f, f->courses);
		if (f->screen == SCR_SETTINGS) return open_item(f, 1);
		return 0;
	}
	return 0;
}

static int SEC_UI tap_in(Front *f, int16_t x, int16_t y);

/* A click for anything a key did: a move or a pick. */
int front_key(Front *f, uint8_t key)
{
	int r = key_in(f, key);
	if (r) snd_play(key == KEY_UP || key == KEY_DOWN || key == KEY_LEFT || key == KEY_RIGHT ? SND_MENU_MOVE : SND_MENU_SELECT);
	return r;
}

int front_tap(Front *f, int16_t x, int16_t y)
{
	int r = tap_in(f, x, y);
	if (r) snd_play(SND_MENU_SELECT);
	return r;
}

static int SEC_UI tap_in(Front *f, int16_t x, int16_t y)
{
	int i;

	if (f->screen == SCR_TITLE) {
		f->screen = SCR_MENU;
		return FR_REDRAW;
	}
	if (f->dialog) {
		i = ui_dialog_hit(&f->dlg, x, y);
		if (i < 0) return 0;
		return close_dialog(f, (uint8_t)i);
	}
	i = ui_menu_hit(&f->menu, x, y);
	if (i < 0) return 0;
	f->menu.sel = (uint8_t)i;
	return open_item(f, (uint8_t)i) | FR_REDRAW;
}
