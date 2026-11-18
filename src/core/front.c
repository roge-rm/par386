#include "front.h"

/* The menu board's felt and name plate in the menu picture (tools/mkart.py). */
#define PLATE_CX 80
#define PLATE_Y 5
#define PLATE_H 13

void front_init(Front *f)
{
	f->screen = SCR_TITLE;
	f->dialog = 0;
	ui_menu_init(&f->menu, "Play a round|Practice|Courses|Settings|Quit", 22, 36, 116, 20);
}

uint16_t front_picture(const Front *f)
{
	return f->screen == SCR_TITLE ? PIC_TITLE : PIC_MENU;
}

void front_draw(const Front *f, Surface *s)
{
	if (f->screen == SCR_TITLE) {
		ui_ctext(s, 80, 146, "PRESS ANY BUTTON", 16, UIC_WHITE, UIC_BLACK);
		return;
	}
	ui_ctext(s, PLATE_CX + 1, PLATE_Y + (PLATE_H - UI_CHAR) / 2 + 1, "CLUBHOUSE", 9, UIC_BRASS_LT, -1);
	ui_ctext(s, PLATE_CX, PLATE_Y + (PLATE_H - UI_CHAR) / 2, "CLUBHOUSE", 9, UIC_BRASS_DK, -1);
	ui_menu_draw(s, &f->menu);
	if (f->dialog)
		ui_dialog_draw(s, &f->dlg);
}

static int open_item(Front *f, uint8_t i)
{
	if (i == MAIN_QUIT) return FR_QUIT;
	switch (i) {
	case MAIN_PLAY: ui_dialog_init(&f->dlg, "Play a round", "Rounds come in a\nlater version.", "OK"); break;
	case MAIN_PRACTICE: ui_dialog_init(&f->dlg, "Practice", "The range and the\ngreen come later.", "OK"); break;
	case MAIN_COURSES: ui_dialog_init(&f->dlg, "Courses", "No courses yet.", "OK"); break;
	default: ui_dialog_init(&f->dlg, "Settings", "Nothing to set yet.", "OK"); break;
	}
	f->dialog = 1;
	return FR_REDRAW;
}

int front_key(Front *f, uint8_t key)
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
			f->dialog = 0;
			return FR_REDRAW;
		}
		return 0;
	}
	switch (key) {
	case KEY_UP: f->oldSel = f->menu.sel; ui_menu_move(&f->menu, -1); return FR_REDRAW | FR_LINES;
	case KEY_DOWN: f->oldSel = f->menu.sel; ui_menu_move(&f->menu, 1); return FR_REDRAW | FR_LINES;
	case KEY_SELECT: return open_item(f, f->menu.sel);
	}
	return 0;
}

int front_tap(Front *f, int16_t x, int16_t y)
{
	int i;

	if (f->screen == SCR_TITLE) {
		f->screen = SCR_MENU;
		return FR_REDRAW;
	}
	if (f->dialog) {
		i = ui_dialog_hit(&f->dlg, x, y);
		if (i < 0) return 0;
		f->dialog = 0;
		return FR_REDRAW;
	}
	i = ui_menu_hit(&f->menu, x, y);
	if (i < 0) return 0;
	f->menu.sel = (uint8_t)i;
	return open_item(f, (uint8_t)i) | FR_REDRAW;
}
