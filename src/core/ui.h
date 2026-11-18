/* DOS-style UI kit: text, bevelled boxes, a menu with a highlight bar and the
 * golf ball cursor, and dialog boxes with a title and buttons. Everything is
 * in logical units, 160 x 160 for the whole screen; gfx.h turns them into
 * pixels. Strings are lists in one string: menu items and buttons are split
 * by '|', dialog lines by '\n'. No platform headers. */
#ifndef UI_H
#define UI_H

#include "gfx.h"

#define UI_CHAR 8            /* a character is 8 x 8 logical units */
#define UI_MAX_ITEMS 8
#define UI_MAX_BUTTONS 3

typedef struct { int16_t x, y, w, h; } URect;

typedef struct {
	const char *items;       /* "Play|Practice|..." */
	uint8_t count, sel;
	int16_t x, y, w;         /* the bar's left, the first line's top, the bar's width */
	int16_t lineH;
} UiMenu;

typedef struct {
	const char *title;
	const char *text;        /* lines split by '\n' */
	const char *buttons;     /* "OK|Cancel" */
	uint8_t count, sel;
	URect box, btn[UI_MAX_BUTTONS];
} UiDialog;

/* Length of part i of a list split by sep, and where it starts. */
const char *SEC_UI ui_part(const char *list, char sep, uint8_t i, int16_t *len);
uint8_t SEC_UI ui_parts(const char *list, char sep);

void SEC_UI ui_text(Surface *s, int16_t x, int16_t y, const char *t, int16_t n, uint8_t c, int8_t shadow);
/* Centred on cx. */
void SEC_UI ui_ctext(Surface *s, int16_t cx, int16_t y, const char *t, int16_t n, uint8_t c, int8_t shadow);
void SEC_UI ui_fill(Surface *s, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c);
/* A one unit frame, light on the top and left, dark on the bottom and right. */
void SEC_UI ui_bevel(Surface *s, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t lt, uint8_t dk);

void SEC_UI ui_menu_init(UiMenu *m, const char *items, int16_t x, int16_t y, int16_t w, int16_t lineH);
/* Moves the highlight by d lines, wrapping round. */
void SEC_UI ui_menu_move(UiMenu *m, int8_t d);
/* The line under (x, y), or -1. */
int SEC_UI ui_menu_hit(const UiMenu *m, int16_t x, int16_t y);
void SEC_UI ui_menu_draw(Surface *s, const UiMenu *m);
/* One line alone, and the area it covers, for redrawing just the lines that changed. */
void SEC_UI ui_menu_draw_line(Surface *s, const UiMenu *m, uint8_t i);
void SEC_UI ui_menu_line_rect(const UiMenu *m, uint8_t i, URect *r);

/* Sets up d and lays it out centred on the screen. */
void SEC_UI ui_dialog_init(UiDialog *d, const char *title, const char *text, const char *buttons);
/* The button under (x, y), or -1. */
int SEC_UI ui_dialog_hit(const UiDialog *d, int16_t x, int16_t y);
void SEC_UI ui_dialog_draw(Surface *s, const UiDialog *d);

#endif
