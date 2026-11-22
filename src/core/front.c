#include "front.h"
#include <stddef.h>
#include "dana.h"
#include "records.h"
#include "sound.h"

/* The menu board's felt and name plate in the menu picture (tools/mkart.py). */
#define PLATE_CX 80
#define PLATE_Y 5
#define PLATE_H 13

/* The record line under the courses screen's preview. */
#define REC_X 16
#define REC_Y 146
#define REC_W 128

#define DANA_LIST_X 32
#define DANA_LIST_W 248
#define DANA_DETAIL_X 292
#define DANA_DETAIL_W 236
#define DANA_PREVIEW_X 304
#define DANA_PREVIEW_Y 46
#define DANA_CENTER_X 112
#define DANA_CENTER_W 336
#define DANA_BOX_Y 6
#define DANA_BOX_H 148
#define DANA_LINE_Y 38
#define DANA_LINE_H 16

enum { PLAYER_NAME, PLAYER_CHARACTER, PLAYER_BACK };
enum { NAME_SPACE = 36, NAME_DEL, NAME_CLEAR, NAME_CASE, NAME_DONE, NAME_CANCEL, NAME_KEYS };
enum { CHARACTER_DONE = 7, CHARACTER_CANCEL, CHARACTER_ROWS };

#define CHARACTER_X 15
#define CHARACTER_Y 25
#define CHARACTER_ROW_H 14
#define CHARACTER_ROW_W 101
#define CHARACTER_BUTTON_Y 130
#define CHARACTER_BUTTON_W 65

static int SEC_DANA dana_surface(const Surface *s);
static void SEC_DANA dana_front_draw(const Front *f, Surface *s, const char *plate);

static const char name_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const uint8_t name_edit_edges[] = { 14, 56, 82, 124, 146 };
static const char *const name_actions[] = { "Space", "Del", "Clear", "Aa", "Done", "Cancel" };
static const PlayerProfile default_player = {
	PLAYER_DEFAULT_NAME, GOLFER_MALE, SKIN_LIGHT, 0,
	OUTFIT_DEFAULT, OUTFIT_DEFAULT, OUTFIT_DEFAULT, OUTFIT_DEFAULT
};

static const char outfit_colours[] = "Default|Navy|Green|Red|Gold|White|Grey";
static const struct {
	const char *label, *values;
	uint8_t offset, count;
} character_options[CHARACTER_DONE] = {
	{ "Gender", "Man|Woman",        offsetof(PlayerProfile, golfer), GOLFER_MODELS },
	{ "Hand",  "Right|Left",         offsetof(PlayerProfile, left),   2 },
	{ "Skin",  "Light|Medium|Dark",  offsetof(PlayerProfile, skin),   SKIN_COLOURS },
	{ "Shoes", outfit_colours, offsetof(PlayerProfile, shoes), OUTFIT_COLOURS },
	{ "Pants", outfit_colours, offsetof(PlayerProfile, pants), OUTFIT_COLOURS },
	{ "Shirt", outfit_colours, offsetof(PlayerProfile, shirt), OUTFIT_COLOURS },
	{ "Hat",   outfit_colours, offsetof(PlayerProfile, hat),   OUTFIT_COLOURS }
};

static void SEC_DANA character_box(uint8_t row, URect *b)
{
	if (row < CHARACTER_DONE) {
		b->x = CHARACTER_X;
		b->y = CHARACTER_Y + row * CHARACTER_ROW_H;
		b->w = CHARACTER_ROW_W;
		b->h = CHARACTER_ROW_H - 1;
	} else {
		b->x = 14 + (row - CHARACTER_DONE) * (CHARACTER_BUTTON_W + 1);
		b->y = CHARACTER_BUTTON_Y;
		b->w = CHARACTER_BUTTON_W;
		b->h = 15;
	}
}

static void SEC_DANA character_draw_row(const Front *f, Surface *s, uint8_t row, URect *b, uint8_t label)
{
	int16_t n;
	const char *value;
	uint8_t selected = row == f->character_row;

	character_box(row, b);
	if (row < CHARACTER_DONE) {
		uint8_t choice = ((const uint8_t *)&f->character)[character_options[row].offset];
		int16_t label_w = row == 0 ? 50 : 42;
		if (label) {
			const char *t = ui_part(character_options[row].label, 0, 0, &n);
			ui_fill(s, b->x, b->y, label_w, b->h, UIC_TITLE);
			ui_text(s, b->x + 1, b->y + 3, t, n, UIC_TEXT_HI, -1);
		}
		value = ui_part(character_options[row].values, '|', choice, &n);
		b->x += label_w;
		b->w -= label_w;
	} else {
		value = ui_part(row == CHARACTER_DONE ? "Done" : "Cancel", 0, 0, &n);
	}
	ui_fill(s, b->x, b->y, b->w, b->h, selected ? UIC_TITLE : UIC_BRASS);
	ui_bevel(s, b->x, b->y, b->w, b->h, selected ? UIC_BAR_LT : UIC_BRASS_LT, UIC_BRASS_DK);
	ui_ctext(s, b->x + b->w / 2, b->y + (b->h - UI_CHAR) / 2 + 1, value, n,
	         selected ? UIC_TEXT_HI : UIC_INK, -1);
}

static void SEC_DANA character_clear_preview(Surface *s, URect *b)
{
	b->x = CHARACTER_PREVIEW_X;
	b->y = CHARACTER_PREVIEW_Y;
	b->w = CHARACTER_PREVIEW_W;
	b->h = CHARACTER_PREVIEW_H;
	ui_fill(s, b->x, b->y, b->w, b->h, UIC_PAPER_DK);
}

static void SEC_DANA character_draw(const Front *f, Surface *s)
{
	URect b;
	uint8_t row;

	character_clear_preview(s, &b);
	ui_bevel(s, b.x - 1, b.y - 1, b.w + 2, b.h + 2, UIC_INK, UIC_BRASS_LT);
	for (row = 0; row < CHARACTER_ROWS; row++) character_draw_row(f, s, row, &b, 1);
}

uint8_t front_character_update(const Front *f, Surface *s, uint8_t old_row, uint8_t preview, URect *dirty)
{
	uint8_t n = 0, i;

	if (old_row != f->character_row) character_draw_row(f, s, old_row, &dirty[n++], 0);
	character_draw_row(f, s, f->character_row, &dirty[n++], 0);
	if (preview) character_clear_preview(s, &dirty[n++]);
	for (i = 0; i < n; i++) {
		dirty[i].x = s->ox + dirty[i].x * s->scale;
		dirty[i].y = s->oy + dirty[i].y * s->scale;
		dirty[i].w *= s->scale;
		dirty[i].h *= s->scale;
	}
	return n;
}

static void SEC_MENU main_menu(Front *f)
{
	if (f->saved)
		ui_menu_init(&f->menu, "Continue round|Play a round|Practice|Courses|Settings|Player|Quit", 16, 30, 128, 16);
	else
		ui_menu_init(&f->menu, "Play a round|Practice|Courses|Settings|Player|Quit", 22, 36, 116, 20);
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
	f->shotmap = 1;
	f->dana = 0;
	f->pending = SCR_GAMES;
	f->oldSel = 0;
	f->courses = 0;
	f->course = 0;
	f->page = 0;
	f->course_list[0] = 0;
	f->tee = 0;
	f->span = SPAN_FULL;
	for (k = 0; k < FRONT_COURSES; k++) {
		uint8_t t, span;
		f->event_best[k] = 0;
		f->tee_count[k] = 0;
		f->hole_count[k] = 0;
		for (t = 0; t < COURSE_MAX_TEES; t++)
			for (span = 0; span < SPAN_COUNT; span++) {
				f->rec_strokes[k][t][span] = 0;
				f->rec_par[k][t][span] = 0;
			}
	}
	f->sound = SOUND_ON;
	f->player = default_player;
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
	f->hole_count[i] = c ? c->hole_count : 0;
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

void front_record(Front *f, uint8_t i, uint8_t tee, uint8_t span, uint16_t strokes, int16_t to_par)
{
	if (i >= FRONT_COURSES || tee >= COURSE_MAX_TEES || span >= SPAN_COUNT) return;
	f->rec_strokes[i][tee][span] = strokes;
	f->rec_par[i][tee][span] = to_par;
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
	uint8_t n = 0, tee = f->tee, k, m = 0, span;
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
	span = f->hole_count[i] == 18 ? f->span : SPAN_FULL;
	if (span == SPAN_FRONT) a = "Front 9";
	else if (span == SPAN_BACK) a = "Back 9";
	strokes = f->rec_strokes[i][tee][span];
	to_par = f->rec_par[i][tee][span];
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

uint16_t front_picture(const Front *f, const Surface *s)
{
	uint8_t title = f->screen == SCR_TITLE;

	if (dana_surface(s)) return title ? PIC_DANA_TITLE : PIC_DANA_MENU;
	if (s->scale == 2 && s->depth == 8) {
		if (s->w == 320 && s->h > 320) return title ? PIC_TALL_TITLE : PIC_TALL_MENU;
		if (s->h == 320 && s->w > 320) return title ? PIC_WIDE_TITLE : PIC_WIDE_MENU;
	}
	return title ? PIC_TITLE : PIC_MENU;
}

/* The board and its controls use the same square in the middle of each picture. */
int front_picture_band(Surface *s, uint16_t id, const uint8_t *d, uint32_t len)
{
	int16_t ox = s->ox, oy = s->oy;
	int r;

	if (id == PIC_DANA_TITLE || id == PIC_DANA_MENU) {
		s->ox = s->oy = 0;
		r = gfx_pic_band(s, d, len);
		s->ox = ox;
		s->oy = oy;
		return r;
	}
	if (id == PIC_TALL_TITLE || id == PIC_TALL_MENU) s->oy -= PIC_EXTEND_PX;
	else if (id == PIC_WIDE_TITLE || id == PIC_WIDE_MENU) s->ox -= PIC_EXTEND_PX;
	else return gfx_pic_band(s, d, len);
	r = gfx_pic_band_crop8(s, d, len);
	s->ox = ox;
	s->oy = oy;
	return r;
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
	uint8_t scale = s->scale;
	int r;

	if (dana_surface(s)) {
		s->ox = DANA_PREVIEW_X;
		s->oy = DANA_PREVIEW_Y;
		s->scale = 2;
	} else {
		s->ox = (int16_t)(ox + PREVIEW_X * scale);
		s->oy = (int16_t)(oy + PREVIEW_Y * scale);
	}
	r = gfx_pic_band(s, d, len);
	s->ox = ox;
	s->oy = oy;
	s->scale = scale;
	return r;
}

/* The keyboard's letter grid, editing keys and confirmation buttons. */
static void SEC_MENU name_key_box(uint8_t key, URect *box, uint8_t wide)
{
	box->h = 11;
	if (key < NAME_SPACE) {
		box->x = (int16_t)(14 + (key / 6) * 22);
		box->y = (int16_t)(46 + (key % 6) * 12);
		box->w = 21;
	} else if (key < NAME_DONE) {
		uint8_t i = key - NAME_SPACE;
		box->x = name_edit_edges[i];
		box->y = 120;
		box->w = name_edit_edges[i + 1] - box->x - 1;
	} else {
		box->x = (int16_t)(14 + (key - NAME_DONE) * 66);
		box->y = 134;
		box->w = 65;
	}
	if (wide) {
		box->x = DANA_CENTER_X + 18 + (int16_t)((box->x - 14) * 50 / 22);
		box->w = (int16_t)((box->w + 1) * 50 / 22 - 2);
	}
}

static char SEC_MENU name_letter(const Front *f, uint8_t key)
{
	char c = name_chars[(key % 6) * 6 + key / 6];
	return f->name_lower && c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static uint8_t SEC_MENU name_len(const char *name)
{
	uint8_t n = 0;
	while (n < PLAYER_NAME_MAX && name[n]) n++;
	return n;
}

static void SEC_MENU name_field(const Front *f, Surface *s, URect *b, uint8_t wide)
{
	uint8_t len = name_len(f->player_name);

	b->x = wide ? DANA_CENTER_X + 18 : 14;
	b->y = wide ? 27 : 26;
	b->w = wide ? 298 : 131;
	b->h = 15;
	ui_fill(s, b->x, b->y, b->w, b->h, UIC_TITLE);
	ui_bevel(s, b->x, b->y, b->w, b->h, UIC_INK, wide ? UIC_WHITE : UIC_PAPER_DK);
	ui_text(s, b->x + 6, b->y + 4, f->player_name, len, UIC_PAPER, -1);
	if (len < PLAYER_NAME_MAX) ui_fill(s, b->x + 6 + len * UI_CHAR, b->y + 12, 6, 1, UIC_PAPER);
}

static void SEC_MENU name_draw_key(const Front *f, Surface *s, uint8_t key, URect *b, uint8_t wide)
{
	char letter;
	int16_t n = 1;
	const char *t;
	uint8_t selected = key == f->name_key;

	if (key < NAME_SPACE) {
		letter = name_letter(f, key);
		t = &letter;
	} else {
		t = key == NAME_CASE && f->name_lower ? "aA" : name_actions[key - NAME_SPACE];
		ui_part(t, 0, 0, &n);
	}
	name_key_box(key, b, wide);
	ui_fill(s, b->x, b->y, b->w, b->h, selected ? UIC_TITLE : UIC_BRASS);
	ui_bevel(s, b->x, b->y, b->w, b->h, selected ? UIC_BAR_LT : UIC_BRASS_LT, UIC_BRASS_DK);
	ui_ctext(s, b->x + b->w / 2, b->y + 2, t, n, selected ? UIC_TEXT_HI : UIC_INK, -1);
}

static void SEC_MENU name_draw(const Front *f, Surface *s, uint8_t wide)
{
	URect b;
	uint8_t key;

	name_field(f, s, &b, wide);
	for (key = 0; key < NAME_KEYS; key++) name_draw_key(f, s, key, &b, wide);
}

uint8_t front_name_update(const Front *f, Surface *s, uint8_t old_key, uint8_t old_lower, URect *dirty)
{
	uint8_t wide = dana_surface(s), n = 1;
	int16_t ox = s->ox, oy = s->oy;
	uint8_t i;

	if (wide) s->ox = s->oy = 0;
	name_field(f, s, &dirty[0], wide);
	if (old_lower != f->name_lower) {
		URect last;
		for (i = 0; i < NAME_KEYS; i++) name_draw_key(f, s, i, &last, wide);
		name_key_box(0, &dirty[n], wide);
		dirty[n].w = last.x + last.w - dirty[n].x;
		dirty[n].h = last.y + last.h - dirty[n].y;
		n++;
	} else if (old_key != f->name_key) {
		name_draw_key(f, s, old_key, &dirty[n++], wide);
		name_draw_key(f, s, f->name_key, &dirty[n++], wide);
	}
	for (i = 0; i < n; i++) {
		dirty[i].x = s->ox + dirty[i].x * s->scale;
		dirty[i].y = s->oy + dirty[i].y * s->scale;
		dirty[i].w *= s->scale;
		dirty[i].h *= s->scale;
	}
	s->ox = ox;
	s->oy = oy;
	return n;
}

static void SEC_MENU event_setup(const Front *f, Surface *s)
{
	const char *name = rec_event_name(f->course < f->courses ? f->course_names[f->course] : "");
	uint8_t finish = f->course < f->courses ? f->event_best[f->course] : 0;
	uint8_t n = 0;
	char best[16] = "Best: ";

	while (name[n]) n++;
	ui_fill(s, 16, 125, 128, 19, UIC_TITLE);
	ui_bevel(s, 16, 125, 128, 19, UIC_INK, UIC_WHITE);
	ui_ctext(s, 80, finish ? 127 : 131, name, n, UIC_TEXT_HI, -1);
	if (!finish) return;
	n = 6;
	if (finish & EVENT_TIED) best[n++] = 'T';
	put_num(best, &n, finish & EVENT_PLACE);
	ui_ctext(s, 80, 135, best, n, UIC_PAPER, -1);
}

void front_draw(const Front *f, Surface *s)
{
	const char *plate = f->screen == SCR_PLAYERS ? "PLAYERS" : f->screen == SCR_COURSES ? "COURSES" :
	                    f->screen == SCR_SETTINGS ? "SETTINGS" : f->screen == SCR_GAMES ? "GAMES" :
	                    f->screen == SCR_PLAYER ? "PLAYER" : f->screen == SCR_PLAYER_NAME ? "NAME" :
	                    f->screen == SCR_CHARACTER ? "CHARACTER" :
	                    f->screen == SCR_PRACTICE ? "PRACTICE" : "CLUBHOUSE";
	int16_t n;

	if (dana_surface(s)) {
		dana_front_draw(f, s, plate);
		return;
	}
	if (f->screen == SCR_TITLE) {
		int16_t oy = s->oy;
		uint16_t id = front_picture(f, s);
		if (id == PIC_TALL_TITLE) s->oy = s->h - 320;
		ui_ctext(s, 80, id == PIC_TITLE ? 146 : 142, "PRESS ANY BUTTON", 16, UIC_WHITE, UIC_BLACK);
		s->oy = oy;
		return;
	}
	for (n = 0; plate[n]; n++) {}
	ui_ctext(s, PLATE_CX + 1, PLATE_Y + (PLATE_H - UI_CHAR) / 2 + 1, plate, n, UIC_BRASS_LT, -1);
	ui_ctext(s, PLATE_CX, PLATE_Y + (PLATE_H - UI_CHAR) / 2, plate, n, UIC_BRASS_DK, -1);
	if (f->screen == SCR_CHARACTER) {
		character_draw(f, s);
		return;
	}
	if (f->screen == SCR_PLAYER_NAME) {
		name_draw(f, s, 0);
		return;
	}
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
	if (f->screen == SCR_PLAYERS && f->mode == MODE_TOURN) event_setup(f, s);
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

static uint8_t SEC_MENU has_nines(const Front *f)
{
	return f->mode < MODE_TOURN && f->course < f->courses && f->hole_count[f->course] == 18;
}

static const char span_names[SPAN_COUNT][8] = { "All 18", "Front 9", "Back 9" };

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
	if (has_nines(f)) {
		for (a = "|Holes: "; *a; a++) f->players_list[n++] = *a;
		for (a = span_names[f->span]; *a; a++) f->players_list[n++] = *a;
	}
	for (a = "|Tee off|Back"; *a; a++) f->players_list[n++] = *a;
	f->players_list[n] = 0;
	f->screen = SCR_PLAYERS;
	/* 7 lines with 4 slots: closer together to stay on the board */
	if (f->mode == MODE_TOURN || (has_nines(f) && slots(f) > 3))
		ui_menu_init(&f->menu, f->players_list, 16, 24, 128, 14);
	else if (slots(f) > 3) ui_menu_init(&f->menu, f->players_list, 16, 30, 128, 16);
	else ui_menu_init(&f->menu, f->players_list, 16, 34, 128, 18);
	f->menu.sel = sel;
}

static int SEC_MENU players_screen(Front *f)
{
	players_menu(f, (uint8_t)(slots(f) + 1 + has_nines(f)));
	return FR_REDRAW;
}

/* The tee set to the next or last, round. */
static void SEC_MENU cycle_tee(Front *f, int8_t d)
{
	uint8_t n = front_tee_count(f);
	f->tee = (uint8_t)((f->tee + n + d) % n);
	players_menu(f, slots(f));
}

static void SEC_MENU cycle_span(Front *f, int8_t d)
{
	f->span = (uint8_t)((f->span + SPAN_COUNT + d) % SPAN_COUNT);
	players_menu(f, (uint8_t)(slots(f) + 1));
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
 * shots (a quick result on the map, or the full scene and flight), where
 * full shots fly, the gimme rule for new rounds, then Back. */
static void SEC_MENU settings_menu(Front *f)
{
	const char *a = "Sound: ", *b = f->sound == SOUND_FX ? "Effects" : f->sound == SOUND_OFF ? "Off" : "On";
	const char *c = "|CPU: ", *d = f->quick ? "Quick" : "Full";
	const char *e = f->shotmap ? "|Shot view: Map" : "|Shot view: 3D";
	const char *g = f->holeout ? "|Hole out|Back" : "|Gimmies: 2 ft|Back";
	uint8_t n = 0, sel = f->screen == SCR_SETTINGS ? f->menu.sel : 0;

	while (*a) f->settings_list[n++] = *a++;
	while (*b) f->settings_list[n++] = *b++;
	while (*c) f->settings_list[n++] = *c++;
	while (*d) f->settings_list[n++] = *d++;
	if (!f->dana) while (*e) f->settings_list[n++] = *e++;
	while (*g) f->settings_list[n++] = *g++;
	f->settings_list[n] = 0;
	f->screen = SCR_SETTINGS;
	ui_menu_init(&f->menu, f->settings_list, 16, 36, 128, 18);
	f->menu.sel = sel < f->menu.count ? sel : f->menu.count - 1;
}

void front_dana(Front *f, uint8_t wide)
{
	if (f->dana == wide) return;
	f->dana = wide;
	if (f->screen == SCR_SETTINGS) settings_menu(f);
}

static void SEC_MENU player_menu(Front *f, uint8_t sel)
{
	const char *a = "Name: ", *b = "|Edit Character|Back";
	uint8_t n = 0, k, len = name_len(f->player.name);

	while (*a) f->player_list[n++] = *a++;
	for (k = 0; k < len; k++) f->player_list[n++] = f->player.name[k] == '|' ? ' ' : f->player.name[k];
	while (*b) f->player_list[n++] = *b++;
	f->player_list[n] = 0;
	f->screen = SCR_PLAYER;
	ui_menu_init(&f->menu, f->player_list, 16, 36, 128, 18);
	f->menu.sel = sel;
}

static int SEC_MENU player_pick(Front *f, uint8_t i)
{
	switch (i) {
	case PLAYER_NAME: {
		uint8_t k;
		for (k = 0; k <= PLAYER_NAME_MAX; k++) f->player_name[k] = f->player.name[k];
		f->player_name[PLAYER_NAME_MAX] = 0;
		f->name_key = NAME_CLEAR;
		f->name_lower = 0;
		f->screen = SCR_PLAYER_NAME;
		return FR_REDRAW;
	}
	case PLAYER_CHARACTER:
		f->character = f->player;
		f->character_row = 0;
		f->screen = SCR_CHARACTER;
		return FR_REDRAW;
	default:
		front_clubhouse(f, f->saved);
		f->menu.sel = (uint8_t)(MAIN_PLAYER + (f->saved ? 1 : 0));
		return FR_REDRAW;
	}
}

static int SEC_DANA character_pick(Front *f, uint8_t row, int8_t d)
{
	if (row < CHARACTER_DONE) {
		uint8_t count = character_options[row].count;
		uint8_t *choice = (uint8_t *)&f->character + character_options[row].offset;
		*choice = (uint8_t)((*choice + count + d) % count);
		f->character_row = row;
		return FR_REDRAW | FR_CHARACTER | FR_CHARACTER_POSE;
	}
	if (row == CHARACTER_DONE) f->player = f->character;
	player_menu(f, PLAYER_CHARACTER);
	return FR_REDRAW | (row == CHARACTER_DONE ? FR_PLAYER : 0);
}

static int SEC_DANA character_key(Front *f, uint8_t key)
{
	switch (key) {
	case KEY_UP: case KEY_DOWN: {
		int8_t d = key == KEY_UP ? -1 : 1;
		f->character_row = (uint8_t)((f->character_row + CHARACTER_ROWS + d) % CHARACTER_ROWS);
		return FR_REDRAW | FR_CHARACTER;
	}
	case KEY_LEFT: case KEY_RIGHT: {
		int8_t d = key == KEY_LEFT ? -1 : 1;
		if (f->character_row < CHARACTER_DONE) return character_pick(f, f->character_row, d);
		f->character_row = f->character_row == CHARACTER_DONE ? CHARACTER_CANCEL : CHARACTER_DONE;
		return FR_REDRAW | FR_CHARACTER;
	}
	case KEY_SELECT: return character_pick(f, f->character_row, 1);
	case KEY_BACK: return character_pick(f, CHARACTER_CANCEL, 1);
	default: return 0;
	}
}

static int SEC_DANA character_tap(Front *f, int16_t x, int16_t y)
{
	uint8_t row;

	for (row = 0; row < CHARACTER_ROWS; row++) {
		URect b;
		character_box(row, &b);
		if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h)
			return character_pick(f, row, 1);
	}
	return 0;
}

static int SEC_MENU name_input(Front *f, uint16_t chr)
{
	uint8_t n = name_len(f->player_name);

	if (chr == '\r' || chr == '\n') {
		const char *name;
		uint8_t first = 0, k;
		while (first < n && f->player_name[first] == ' ') first++;
		while (n > first && f->player_name[n - 1] == ' ') n--;
		name = first == n ? default_player.name : f->player_name + first;
		n = first == n ? name_len(default_player.name) : (uint8_t)(n - first);
		for (k = 0; k < n; k++) f->player.name[k] = name[k];
		for (; k <= PLAYER_NAME_MAX; k++) f->player.name[k] = 0;
		player_menu(f, PLAYER_NAME);
		return FR_REDRAW | FR_PLAYER;
	}
	if (chr == 27) {
		player_menu(f, PLAYER_NAME);
		return FR_REDRAW;
	}
	if (chr == '\b') {
		if (n) f->player_name[n - 1] = 0;
		return FR_REDRAW | FR_NAME;
	}
	if (chr < 32 || chr > 126 || chr == '|') return 0;
	if (n < PLAYER_NAME_MAX) {
		f->player_name[n] = (char)chr;
		f->player_name[n + 1] = 0;
	}
	return FR_REDRAW | FR_NAME;
}

static int SEC_MENU name_pick(Front *f, uint8_t key)
{
	if (key < NAME_SPACE) return name_input(f, (uint8_t)name_letter(f, key));
	switch (key) {
	case NAME_SPACE: return name_input(f, ' ');
	case NAME_DEL: return name_input(f, '\b');
	case NAME_CLEAR: f->player_name[0] = 0; return FR_REDRAW | FR_NAME;
	case NAME_CASE: f->name_lower ^= 1; return FR_REDRAW | FR_NAME;
	case NAME_DONE: return name_input(f, '\r');
	default: return name_input(f, 27);
	}
}

int front_text(Front *f, uint16_t chr)
{
	int r;
	if (f->screen != SCR_PLAYER_NAME) return 0;
	r = name_input(f, chr);
	if (r) snd_play(SND_MENU_SELECT);
	return r;
}

static int SEC_MENU open_item(Front *f, uint8_t i)
{
	if (f->screen == SCR_PLAYER) return player_pick(f, i);
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
		if (i == 2 && !f->dana) {
			f->shotmap ^= 1;
			settings_menu(f);
			return FR_REDRAW | FR_SHOT;
		}
		if (i == (f->dana ? 2 : 3)) {
			f->holeout ^= 1;
			settings_menu(f);
			return FR_REDRAW | FR_GIMMES;
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
		if (has_nines(f) && i == slots(f) + 1) {
			f->oldSel = i;
			cycle_span(f, 1);
			return FR_REDRAW | FR_LINES;
		}
		if (i == slots(f) + 1 + has_nines(f)) return tee_off(f);
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
	case MAIN_PLAYER:
		player_menu(f, PLAYER_NAME);
		return FR_REDRAW;
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
	if (f->screen == SCR_CHARACTER) return character_key(f, key);
	if (f->screen == SCR_PLAYER_NAME) {
		int16_t d;
		switch (key) {
		case KEY_UP: d = -1; break;
		case KEY_DOWN: d = 1; break;
		case KEY_LEFT: d = -6; break;
		case KEY_RIGHT: d = 6; break;
		case KEY_SELECT: return name_pick(f, f->name_key);
		case KEY_BACK: return name_pick(f, NAME_CANCEL);
		default: return 0;
		}
		f->name_key = (uint8_t)((f->name_key + NAME_KEYS + d) % NAME_KEYS);
		return FR_REDRAW | FR_NAME;
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
	if (f->screen == SCR_PLAYERS && (key == KEY_LEFT || key == KEY_RIGHT) && f->menu.sel <= slots(f) + has_nines(f)) {
		if (f->menu.sel < slots(f)) cycle_slot(f, f->menu.sel, key == KEY_LEFT ? -1 : 1);
		else if (f->menu.sel == slots(f)) cycle_tee(f, key == KEY_LEFT ? -1 : 1);
		else cycle_span(f, key == KEY_LEFT ? -1 : 1);
		f->oldSel = f->menu.sel;
		return FR_REDRAW | FR_LINES;
	}
	switch (key) {
	/* on the courses screen the preview changes too, so all of it is redrawn */
	case KEY_UP: f->oldSel = f->menu.sel; ui_menu_move(&f->menu, -1); return FR_REDRAW | (f->screen == SCR_COURSES ? 0 : FR_LINES);
	case KEY_DOWN: f->oldSel = f->menu.sel; ui_menu_move(&f->menu, 1); return FR_REDRAW | (f->screen == SCR_COURSES ? 0 : FR_LINES);
	case KEY_SELECT: return open_item(f, f->menu.sel);
	case KEY_BACK:
		if (f->screen == SCR_PLAYERS) return open_item(f, (uint8_t)(f->menu.count - 1));
		if (f->screen == SCR_GAMES) return open_item(f, MODE_RANGE);
		if (f->screen == SCR_PRACTICE) return open_item(f, 2);
		if (f->screen == SCR_COURSES) return open_item(f, (uint8_t)(f->menu.count - 1));
		if (f->screen == SCR_SETTINGS) return open_item(f, (uint8_t)(f->menu.count - 1));
		if (f->screen == SCR_PLAYER) return open_item(f, PLAYER_BACK);
		return 0;
	}
	return 0;
}

/* A click for anything a key did: a move or a pick. */
int front_key(Front *f, uint8_t key)
{
	int r = key_in(f, key);
	if (r) snd_play(key == KEY_UP || key == KEY_DOWN || key == KEY_LEFT || key == KEY_RIGHT ? SND_MENU_MOVE : SND_MENU_SELECT);
	return r;
}

static int SEC_MENU tap_in(Front *f, int16_t x, int16_t y);

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
	if (f->screen == SCR_CHARACTER) return character_tap(f, x, y);
	if (f->screen == SCR_PLAYER_NAME) {
		uint8_t key;
		for (key = 0; key < NAME_KEYS; key++) {
			URect b;
			name_key_box(key, &b, 0);
			if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
				f->name_key = key;
				return name_pick(f, key);
			}
		}
		return 0;
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

static int dana_surface(const Surface *s)
{
	return s->scale == 1 && s->w == DANA_SCREEN_W && s->h == 160;
}

static void SEC_DANA dana_well(Surface *s, int16_t x, int16_t y, int16_t w, const char *t, uint8_t bright)
{
	int16_t n;

	ui_part(t, 0, 0, &n);
	ui_fill(s, x, y, w, 15, UIC_TITLE);
	ui_bevel(s, x, y, w, 15, UIC_INK, UIC_WHITE);
	ui_ctext(s, x + w / 2, y + 4, t, n, bright ? UIC_TEXT_HI : UIC_PAPER, -1);
}

static void SEC_DANA dana_box(Surface *s, int16_t x, int16_t w, const char *title)
{
	ui_fill(s, x + 3, DANA_BOX_Y + 3, w, DANA_BOX_H, UIC_BLACK);
	ui_fill(s, x, DANA_BOX_Y, w, DANA_BOX_H, UIC_PAPER_DK);
	ui_bevel(s, x, DANA_BOX_Y, w, DANA_BOX_H, UIC_WHITE, UIC_INK);
	dana_well(s, x + 4, DANA_BOX_Y + 4, w - 8, title, 1);
}

/* Keep the row order for the keyboard as it moves between the two boards. */
static uint8_t SEC_DANA dana_menus(const Front *f, UiMenu *left, UiMenu *right)
{
	uint8_t split = f->screen == SCR_PLAYERS ? slots(f) : 0;
	uint8_t pair = split || f->screen == SCR_MENU || f->screen == SCR_COURSES;

	*left = f->menu;
	left->x = (pair ? DANA_LIST_X : DANA_CENTER_X) + 8;
	left->w = (pair ? DANA_LIST_W : DANA_CENTER_W) - 16;
	left->y = DANA_LINE_Y;
	left->lineH = DANA_LINE_H;
	if (split) {
		int16_t n;
		*right = *left;
		right->items = ui_part(f->menu.items, '|', split, &n);
		right->x = DANA_DETAIL_X + 8;
		right->w = DANA_DETAIL_W - 16;
		right->count -= split;
		right->sel = f->menu.sel < split ? 0xff : (uint8_t)(f->menu.sel - split);
		left->count = split;
		if (left->sel >= split) left->sel = 0xff;
	}
	return split;
}

static void SEC_DANA dana_course_detail(const Front *f, Surface *s)
{
	uint8_t preview = f->screen == SCR_COURSES ? line_course(f, f->menu.sel) : 0xff;
	uint8_t pick = preview == 0xff ? f->course : preview;
	char line[20];
	int16_t n;

	if (pick >= f->courses) {
		dana_well(s, DANA_DETAIL_X + 12, 42, DANA_DETAIL_W - 24, "No courses installed", 0);
		return;
	}
	dana_well(s, DANA_DETAIL_X + 8, 28, DANA_DETAIL_W - 16, f->course_names[pick], 1);
	if (preview != 0xff) {
		ui_fill(s, DANA_PREVIEW_X, DANA_PREVIEW_Y, PREVIEW_W * 2, PREVIEW_H * 2, UIC_BLACK);
		ui_bevel(s, DANA_PREVIEW_X - 1, DANA_PREVIEW_Y - 1, PREVIEW_W * 2 + 2, PREVIEW_H * 2 + 2, UIC_INK, UIC_WHITE);
		if (f->scenery[pick] == SCENERY_UNKNOWN)
			ui_ctext(s, DANA_PREVIEW_X + PREVIEW_W, DANA_PREVIEW_Y + PREVIEW_H - 4, "ON CARD", 7, UIC_WHITE, -1);
	} else {
		if (f->hole_count[pick]) {
			uint8_t i = 0;
			put_num(line, &i, f->hole_count[pick]);
			ui_text(s, DANA_DETAIL_X + 16, 54, "Holes", 5, UIC_INK, -1);
			ui_text(s, DANA_DETAIL_X + 88, 54, line, i, UIC_INK, -1);
		}
		if (f->tee_count[pick]) {
			const char *tee = front_tee_name(f, f->tee < f->tee_count[pick] ? f->tee : 0);
			ui_part(tee, 0, 0, &n);
			ui_text(s, DANA_DETAIL_X + 16, 72, "Tees", 4, UIC_INK, -1);
			ui_text(s, DANA_DETAIL_X + 88, 72, tee, n, UIC_INK, -1);
		}
		ui_text(s, DANA_DETAIL_X + 16, 90, "Player", 6, UIC_INK, -1);
		ui_text(s, DANA_DETAIL_X + 88, 90, f->player.name, name_len(f->player.name), UIC_INK, -1);
	}
	front_record_line(f, pick, line);
	dana_well(s, DANA_DETAIL_X + 12, 120, DANA_DETAIL_W - 24, line[0] ? line : "No record yet", 0);
}

static void SEC_DANA dana_event(const Front *f, Surface *s)
{
	const char *name = rec_event_name(f->course < f->courses ? f->course_names[f->course] : "");
	uint8_t finish = f->course < f->courses ? f->event_best[f->course] : 0;
	char best[16] = "Best: ";
	uint8_t n = 6;

	dana_well(s, DANA_DETAIL_X + 8, 106, DANA_DETAIL_W - 16, name, 1);
	if (finish) {
		if (finish & EVENT_TIED) best[n++] = 'T';
		put_num(best, &n, finish & EVENT_PLACE);
		best[n] = 0;
		dana_well(s, DANA_DETAIL_X + 8, 124, DANA_DETAIL_W - 16, best, 0);
	}
}

static void dana_front_draw(const Front *f, Surface *s, const char *plate)
{
	int16_t ox = s->ox, oy = s->oy;
	UiMenu left, right;
	uint8_t split;

	s->ox = s->oy = 0;
	if (f->screen == SCR_TITLE) {
		dana_well(s, 208, 141, 144, "PRESS ANY BUTTON", 1);
	} else if (f->screen == SCR_PLAYER_NAME) {
		dana_box(s, DANA_CENTER_X, DANA_CENTER_W, "NAME");
		name_draw(f, s, 1);
	} else if (f->screen == SCR_CHARACTER) {
		dana_box(s, (DANA_SCREEN_W - 176) / 2, 176, plate);
		s->ox = ox;
		s->oy = oy;
		character_draw(f, s);
	} else {
		split = dana_menus(f, &left, &right);
		dana_box(s, left.x - 8, left.w + 16, plate);
		ui_menu_draw(s, &left);
		if (split) {
			dana_box(s, DANA_DETAIL_X, DANA_DETAIL_W, "ROUND");
			ui_menu_draw(s, &right);
			if (f->mode == MODE_TOURN) dana_event(f, s);
		} else if (f->screen == SCR_MENU || f->screen == SCR_COURSES) {
			dana_box(s, DANA_DETAIL_X, DANA_DETAIL_W, "COURSE");
			dana_course_detail(f, s);
		}
		if (f->dialog) {
			s->ox = (DANA_SCREEN_W - 160) / 2;
			ui_dialog_draw(s, &f->dlg);
		}
	}
	s->ox = ox;
	s->oy = oy;
}

int front_dana_tap(Front *f, int16_t x, int16_t y)
{
	UiMenu left, right;
	uint8_t split;
	int i;

	if (f->screen == SCR_TITLE || f->screen == SCR_CHARACTER || f->dialog)
		return front_tap(f, x - (DANA_SCREEN_W - 160) / 2, y);
	if (f->screen == SCR_PLAYER_NAME) {
		uint8_t key;
		for (key = 0; key < NAME_KEYS; key++) {
			URect b;
			name_key_box(key, &b, 1);
			if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
				name_key_box(key, &b, 0);
				return front_tap(f, b.x, b.y);
			}
		}
		return 0;
	}
	split = dana_menus(f, &left, &right);
	i = ui_menu_hit(&left, x, y);
	if (i < 0 && split) {
		i = ui_menu_hit(&right, x, y);
		if (i >= 0) i += split;
	}
	return i < 0 ? 0 : front_tap(f, f->menu.x, f->menu.y + i * f->menu.lineH);
}
