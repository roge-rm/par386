#include "front.h"
#include "sound.h"

/* The menu board's felt and name plate in the menu picture (tools/mkart.py). */
#define PLATE_CX 80
#define PLATE_Y 5
#define PLATE_H 13

/* The record line under the courses screen's preview. */
#define REC_X 16
#define REC_Y 146
#define REC_W 128

static void SEC_MENU main_menu(Front *f)
{
	if (f->saved)
		ui_menu_init(&f->menu, "Continue round|Play a round|Practice|Courses|Settings|Quit", 16, 34, 128, 18);
	else
		ui_menu_init(&f->menu, "Play a round|Practice|Courses|Settings|Quit", 22, 36, 116, 20);
}

void front_init(Front *f)
{
	uint8_t k;

	f->screen = SCR_TITLE;
	f->dialog = DLG_NONE;
	f->saved = 0;
	f->players = 1;
	f->mode = MODE_STROKE;
	f->slot[0] = CPU_HUMAN;
	f->slot[1] = f->slot[2] = f->slot[3] = SLOT_OFF;
	f->cpu[0] = f->cpu[1] = f->cpu[2] = f->cpu[3] = CPU_HUMAN;
	f->quick = 1;
	f->pending = SCR_GAMES;
	f->oldSel = 0;
	f->courses = 0;
	f->course = 0;
	f->page = 0;
	f->course_list[0] = 0;
	f->tee = 0;
	for (k = 0; k < FRONT_COURSES; k++) {
		uint8_t t;
		f->tee_count[k] = 0;
		for (t = 0; t < COURSE_MAX_TEES; t++) f->rec_strokes[k][t] = 0;
	}
	f->sound = SOUND_ON;
	main_menu(f);
}

void front_courses(Front *f, const char *const *names, const uint8_t *scenery, uint8_t count, uint8_t pick)
{
	uint8_t i, k;

	if (count > FRONT_COURSES) count = FRONT_COURSES;
	for (i = 0; i < count; i++) {
		for (k = 0; names[i][k] && k < 23; k++) f->course_names[i][k] = names[i][k] == '|' ? ' ' : names[i][k];
		f->course_names[i][k] = 0;
		f->scenery[i] = scenery ? scenery[i] : SCENERY_UNKNOWN;
	}
	f->courses = count;
	f->course = pick < count ? pick : 0;
	f->page = 0;
}

/* Courses a page of the courses screen shows: all of them if they fit with Back, else FRONT_PAGE. */
static uint8_t SEC_MENU per_page(const Front *f)
{
	return (uint8_t)(f->courses <= FRONT_PAGE + 1 ? f->courses : FRONT_PAGE);
}

/* The course on line i of the courses screen, or 0xFF for More and Back. */
static uint8_t SEC_MENU line_course(const Front *f, uint8_t i)
{
	uint16_t c = (uint16_t)f->page * per_page(f) + i;
	return i < per_page(f) && c < f->courses ? (uint8_t)c : 0xFF;
}

/* Lines on the page before More and Back. */
static uint8_t SEC_MENU page_lines(const Front *f)
{
	uint8_t n = (uint8_t)(f->courses - f->page * per_page(f));
	return n < per_page(f) ? n : per_page(f);
}

void front_clubhouse(Front *f, uint8_t saved)
{
	f->screen = SCR_MENU;
	f->dialog = DLG_NONE;
	f->saved = saved;
	main_menu(f);
}

void front_course_tees(Front *f, uint8_t i, const Course *c)
{
	uint8_t t, k;
	if (i >= FRONT_COURSES) return;
	f->tee_count[i] = c ? c->tee_count : 0;
	for (t = 0; c && t < c->tee_count && t < COURSE_MAX_TEES; t++)
		for (k = 0; k <= TEE_NAME_MAX; k++) f->tee_names[i][t][k] = c->tee_names[t][k];
}

/* Generic names for a course whose tee sets aren't known yet. */
static const char tee_generic[3][8] = { "Back", "Middle", "Forward" };

uint8_t front_tee_count(const Front *f)
{
	return f->course < f->courses && f->tee_count[f->course] ? f->tee_count[f->course] : 3;
}

const char *front_tee_name(const Front *f, uint8_t t)
{
	if (f->course < f->courses && f->tee_count[f->course]) return f->tee_names[f->course][t];
	return tee_generic[t < 3 ? t : 2];
}

void front_record(Front *f, uint8_t i, uint8_t tee, uint16_t strokes, int16_t to_par)
{
	if (i >= FRONT_COURSES || tee >= COURSE_MAX_TEES) return;
	f->rec_strokes[i][tee] = strokes;
	f->rec_par[i][tee] = to_par;
}

static void SEC_MENU put_num(char *t, uint8_t *n, uint16_t v)
{
	char b[6];
	int8_t k = 0;
	do { b[k++] = (char)('0' + v % 10); v /= 10; } while (v);
	while (k) t[(*n)++] = b[--k];
}

void front_record_line(const Front *f, uint8_t i, char *t)
{
	uint8_t n = 0, tee = f->tee, k, m = 0;
	uint16_t strokes;
	int16_t to_par;
	const char *a = "Record";
	char r[12];

	t[0] = 0;
	if (i >= FRONT_COURSES) return;
	if (f->tee_count[i]) {
		if (tee >= f->tee_count[i]) tee = (uint8_t)(f->tee_count[i] - 1);
		a = f->tee_names[i][tee];
	}
	strokes = f->rec_strokes[i][tee];
	to_par = f->rec_par[i][tee];
	if (!strokes || strokes > 999) return;
	/* "Medal 68 (-4)", the name cut to fit the 16 characters of the well */
	r[m++] = ' ';
	put_num(r, &m, strokes);
	r[m++] = ' ';
	r[m++] = '(';
	if (!to_par) {
		r[m++] = 'E';
	} else {
		r[m++] = to_par > 0 ? '+' : '-';
		put_num(r, &m, (uint16_t)(to_par > 99 ? 99 : to_par < -99 ? 99 : to_par < 0 ? -to_par : to_par));
	}
	r[m++] = ')';
	for (k = 0; a[k] && k < 16 - m; k++) t[n++] = a[k];
	for (k = 0; k < m; k++) t[n++] = r[k];
	t[n] = 0;
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

	if (f->screen != SCR_COURSES || f->dialog || line_course(f, f->menu.sel) == 0xFF) return 0;
	sc = f->scenery[line_course(f, f->menu.sel)];
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
	                    f->screen == SCR_SETTINGS ? "SETTINGS" : f->screen == SCR_GAMES ? "GAMES" :
	                    f->screen == SCR_PRACTICE ? "PRACTICE" : "CLUBHOUSE";
	int16_t n;

	if (f->screen == SCR_TITLE) {
		ui_ctext(s, 80, 146, "PRESS ANY BUTTON", 16, UIC_WHITE, UIC_BLACK);
		return;
	}
	for (n = 0; plate[n]; n++) {}
	ui_ctext(s, PLATE_CX + 1, PLATE_Y + (PLATE_H - UI_CHAR) / 2 + 1, plate, n, UIC_BRASS_LT, -1);
	ui_ctext(s, PLATE_CX, PLATE_Y + (PLATE_H - UI_CHAR) / 2, plate, n, UIC_BRASS_DK, -1);
	if (f->screen == SCR_COURSES && line_course(f, f->menu.sel) != 0xFF) {
		uint8_t ci = line_course(f, f->menu.sel);
		/* the preview's frame, and a dark box the picture goes in, if there is one */
		ui_bevel(s, PREVIEW_X - 1, PREVIEW_Y - 1, PREVIEW_W + 2, PREVIEW_H + 2, UIC_BAR_DK, UIC_BAR_LT);
		ui_fill(s, PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, UIC_BLACK);
		if (f->scenery[ci] == SCENERY_UNKNOWN)
			ui_ctext(s, PREVIEW_X + PREVIEW_W / 2, PREVIEW_Y + (PREVIEW_H - UI_CHAR) / 2, "ON CARD", 7, UIC_TEXT, -1);
		/* the course record in a sunken well, as the play panel's readouts */
		{
			char line[20];
			const char *t;
			front_record_line(f, ci, line);
			t = line[0] ? line : "No record yet";
			for (n = 0; t[n]; n++) {}
			ui_fill(s, REC_X + 1, REC_Y + 1, REC_W - 2, 9, UIC_TITLE);
			ui_bevel(s, REC_X, REC_Y, REC_W, 11, UIC_INK, UIC_WHITE);
			ui_ctext(s, REC_X + REC_W / 2, REC_Y + 2, t, n, line[0] ? UIC_TEXT_HI : UIC_PAPER, -1);
		}
	}
	ui_menu_draw(s, &f->menu);
	if (f->dialog)
		ui_dialog_draw(s, &f->dlg);
}

/* Slots shown on the players screen for the mode: 2 in match play, else 4. */
static uint8_t SEC_MENU slots(const Front *f)
{
	return (uint8_t)(f->mode == MODE_MATCH ? 2 : RULES_PLAYERS);
}

/* Whether slot i may be off in the mode: the first never; in match play,
 * best ball and the first two in skins, never. */
static uint8_t SEC_MENU may_be_off(const Front *f, uint8_t i)
{
	if (i == 0 || f->mode == MODE_MATCH || f->mode == MODE_BEST) return 0;
	return f->mode != MODE_SKINS || i >= 2;
}

/* What plays a slot, CPU_* then off. */
static const char slot_names[CPU_LEVELS + 1][9] = { "Human", "CPU Easy", "CPU Fair", "CPU Good", "CPU Pro", "Off" };

/* The players screen's lines, the slots then Tee off and Back, keeping the highlight. */
static void SEC_MENU players_menu(Front *f, uint8_t sel)
{
	uint8_t i, n = 0;
	const char *a;

	for (i = 0; i < slots(f); i++) {
		uint8_t k = f->slot[i];
		if (k == SLOT_OFF && !may_be_off(f, i)) k = f->slot[i] = i ? CPU_FAIR : CPU_HUMAN;
		f->players_list[n++] = 'P';
		f->players_list[n++] = (char)('1' + i);
		f->players_list[n++] = ' ';
		f->players_list[n++] = ' ';
		for (a = slot_names[k == SLOT_OFF ? CPU_LEVELS : k]; *a; a++) f->players_list[n++] = *a;
		f->players_list[n++] = '|';
	}
	/* the tee set, changed as the slots are; the name cut to the 16 characters of the line */
	if (f->tee >= front_tee_count(f)) f->tee = (uint8_t)(front_tee_count(f) - 1);
	for (a = "Tees: "; *a; a++) f->players_list[n++] = *a;
	for (a = front_tee_name(f, f->tee), i = 0; a[i] && i < 10; i++) f->players_list[n++] = a[i] == '|' ? ' ' : a[i];
	for (a = "|Tee off|Back"; *a; a++) f->players_list[n++] = *a;
	f->players_list[n] = 0;
	f->screen = SCR_PLAYERS;
	/* 7 lines with 4 slots: closer together to stay on the board */
	if (slots(f) > 3) ui_menu_init(&f->menu, f->players_list, 16, 30, 128, 16);
	else ui_menu_init(&f->menu, f->players_list, 16, 34, 128, 18);
	f->menu.sel = sel;
}

static int SEC_MENU players_screen(Front *f)
{
	players_menu(f, (uint8_t)(slots(f) + 1));
	return FR_REDRAW;
}

/* The tee set to the next or last, round. */
static void SEC_MENU cycle_tee(Front *f, int8_t d)
{
	uint8_t n = front_tee_count(f);
	f->tee = (uint8_t)((f->tee + n + d) % n);
	players_menu(f, slots(f));
}

/* Slot i to the next or last kind of player that's allowed. */
static void SEC_MENU cycle_slot(Front *f, uint8_t i, int8_t d)
{
	uint8_t k = f->slot[i] == SLOT_OFF ? CPU_LEVELS : f->slot[i];
	do {
		k = (uint8_t)((k + CPU_LEVELS + 1 + d) % (CPU_LEVELS + 1));
	} while (k == CPU_LEVELS && !may_be_off(f, i));
	f->slot[i] = k == CPU_LEVELS ? SLOT_OFF : k;
	players_menu(f, i);
}

/* The round's players from the slots: the ones not off, in order. */
static int SEC_MENU tee_off(Front *f)
{
	uint8_t i, n = 0;
	for (i = 0; i < slots(f); i++)
		if (f->slot[i] != SLOT_OFF) f->cpu[n++] = f->slot[i];
	f->players = n;
	return FR_PLAY;
}

static int SEC_MENU games_screen(Front *f)
{
	f->screen = SCR_GAMES;
	ui_menu_init(&f->menu, "Stroke play|Match play|Skins|Best ball|Tournament|Back", 16, 34, 128, 18);
	f->menu.sel = f->mode < MODE_RANGE ? f->mode : 0;
	return FR_REDRAW;
}

static int SEC_MENU practice_screen(Front *f)
{
	f->screen = SCR_PRACTICE;
	ui_menu_init(&f->menu, "Driving range|Putting green|Back", 16, 36, 128, 20);
	f->menu.sel = f->mode == MODE_GREEN;
	return FR_REDRAW;
}

/* The courses screen's page: its names, More if there are pages, Back; the highlight on line sel. */
static void SEC_MENU courses_page(Front *f, uint8_t sel)
{
	uint8_t i, k, n = 0;
	const char *b = f->courses > per_page(f) ? "More|Back" : "Back";

	for (i = 0; i < page_lines(f); i++) {
		const char *a = f->course_names[line_course(f, i)];
		for (k = 0; a[k]; k++) f->course_list[n++] = a[k];
		f->course_list[n++] = '|';
	}
	while (*b) f->course_list[n++] = *b++;
	f->course_list[n] = 0;
	f->screen = SCR_COURSES;
	/* tighter lines to leave room for the preview below */
	ui_menu_init(&f->menu, f->course_list, 16, 24, 128, 14);
	f->menu.sel = sel;
}

static int SEC_MENU courses_screen(Front *f)
{
	f->page = (uint8_t)(f->course / per_page(f));
	courses_page(f, (uint8_t)(f->course % per_page(f)));
	return FR_REDRAW;
}

/* The settings screen's lines: the sound setting, the computer golfers'
 * shots (a quick result on the map, or the full scene and flight), then
 * Back. */
static void SEC_MENU settings_menu(Front *f)
{
	const char *a = "Sound: ", *b = f->sound == SOUND_FX ? "Effects" : f->sound == SOUND_OFF ? "Off" : "On";
	const char *c = "|CPU: ", *d = f->quick ? "Quick" : "Full", *e = "|Back";
	uint8_t n = 0, sel = f->screen == SCR_SETTINGS ? f->menu.sel : 0;

	while (*a) f->settings_list[n++] = *a++;
	while (*b) f->settings_list[n++] = *b++;
	while (*c) f->settings_list[n++] = *c++;
	while (*d) f->settings_list[n++] = *d++;
	while (*e) f->settings_list[n++] = *e++;
	f->settings_list[n] = 0;
	f->screen = SCR_SETTINGS;
	ui_menu_init(&f->menu, f->settings_list, 16, 36, 128, 20);
	f->menu.sel = sel;
}

static int SEC_MENU open_item(Front *f, uint8_t i)
{
	if (f->screen == SCR_SETTINGS) {
		if (i == 0) {
			f->sound = (uint8_t)((f->sound + 1) % SOUND_MODES);
			settings_menu(f);
			return FR_REDRAW | FR_SOUND;
		}
		if (i == 1) {
			f->quick ^= 1;
			settings_menu(f);
			return FR_REDRAW | FR_CPU;
		}
		front_clubhouse(f, f->saved);
		f->menu.sel = (uint8_t)(MAIN_SETTINGS + (f->saved ? 1 : 0));
		return FR_REDRAW;
	}
	if (f->screen == SCR_COURSES) {
		if (f->courses > per_page(f) && i == page_lines(f)) {
			/* More: the next page, round to the first */
			f->page = (uint8_t)((f->page + 1) * per_page(f) < f->courses ? f->page + 1 : 0);
			courses_page(f, page_lines(f));
			return FR_REDRAW;
		}
		if (line_course(f, i) != 0xFF) f->course = line_course(f, i);
		front_clubhouse(f, f->saved);
		f->menu.sel = (uint8_t)(MAIN_COURSES + (f->saved ? 1 : 0));
		return FR_REDRAW;
	}
	if (f->screen == SCR_PLAYERS) {
		if (i < slots(f)) {
			f->oldSel = i;
			cycle_slot(f, i, 1);
			return FR_REDRAW | FR_LINES;
		}
		if (i == slots(f)) {
			f->oldSel = i;
			cycle_tee(f, 1);
			return FR_REDRAW | FR_LINES;
		}
		if (i == slots(f) + 1) return tee_off(f);
		games_screen(f);
		return FR_REDRAW;
	}
	if (f->screen == SCR_GAMES) {
		if (i >= MODE_RANGE) {
			front_clubhouse(f, f->saved);
			f->menu.sel = f->saved;
			return FR_REDRAW;
		}
		f->mode = i;
		return players_screen(f);
	}
	if (f->screen == SCR_PRACTICE) {
		if (i >= 2) {
			front_clubhouse(f, f->saved);
			f->menu.sel = (uint8_t)(MAIN_PRACTICE + (f->saved ? 1 : 0));
			return FR_REDRAW;
		}
		f->mode = (uint8_t)(MODE_RANGE + i);
		f->players = 1;
		f->cpu[0] = CPU_HUMAN;
		return FR_PLAY;
	}
	i = front_action(f, i);
	if (i == MAIN_QUIT) return FR_QUIT;
	if (i == MAIN_CONTINUE) return FR_CONTINUE;
	if (i == MAIN_PLAY || i == MAIN_PRACTICE) {
		f->pending = i == MAIN_PLAY ? SCR_GAMES : SCR_PRACTICE;
		if (!f->saved) return i == MAIN_PLAY ? games_screen(f) : practice_screen(f);
		ui_dialog_init(&f->dlg, "New round", "The saved round\nwill be lost.", "OK|Cancel");
		f->dialog = DLG_NEW_ROUND;
		return FR_REDRAW;
	}
	switch (i) {
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
static int SEC_MENU close_dialog(Front *f, uint8_t i)
{
	uint8_t d = f->dialog;
	f->dialog = DLG_NONE;
	if (d == DLG_NEW_ROUND && i == 0) return f->pending == SCR_PRACTICE ? practice_screen(f) : games_screen(f);
	return FR_REDRAW;
}

void front_message(Front *f, const char *title, const char *text)
{
	f->screen = SCR_MENU;
	ui_dialog_init(&f->dlg, title, text, "OK");
	f->dialog = DLG_MESSAGE;
}

static int SEC_MENU key_in(Front *f, uint8_t key)
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
	/* left and right change a player slot */
	if (f->screen == SCR_PLAYERS && (key == KEY_LEFT || key == KEY_RIGHT) && f->menu.sel <= slots(f)) {
		if (f->menu.sel < slots(f)) cycle_slot(f, f->menu.sel, key == KEY_LEFT ? -1 : 1);
		else cycle_tee(f, key == KEY_LEFT ? -1 : 1);
		f->oldSel = f->menu.sel;
		return FR_REDRAW | FR_LINES;
	}
	switch (key) {
	/* on the courses screen the preview changes too, so all of it is redrawn */
	case KEY_UP: f->oldSel = f->menu.sel; ui_menu_move(&f->menu, -1); return FR_REDRAW | (f->screen == SCR_COURSES ? 0 : FR_LINES);
	case KEY_DOWN: f->oldSel = f->menu.sel; ui_menu_move(&f->menu, 1); return FR_REDRAW | (f->screen == SCR_COURSES ? 0 : FR_LINES);
	case KEY_SELECT: return open_item(f, f->menu.sel);
	case KEY_BACK:
		if (f->screen == SCR_PLAYERS) return open_item(f, (uint8_t)(slots(f) + 2));
		if (f->screen == SCR_GAMES) return open_item(f, MODE_RANGE);
		if (f->screen == SCR_PRACTICE) return open_item(f, 2);
		if (f->screen == SCR_COURSES) return open_item(f, (uint8_t)(f->menu.count - 1));
		if (f->screen == SCR_SETTINGS) return open_item(f, 2);
		return 0;
	}
	return 0;
}

static int SEC_MENU tap_in(Front *f, int16_t x, int16_t y);

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

static int SEC_MENU tap_in(Front *f, int16_t x, int16_t y)
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
	{
		/* a slot or the tee set changed by a tap: the old highlight's line is redrawn too */
		uint8_t old = f->menu.sel;
		int r;
		f->menu.sel = (uint8_t)i;
		r = open_item(f, (uint8_t)i);
		if (r & FR_LINES) f->oldSel = old;
		return r | FR_REDRAW;
	}
}
