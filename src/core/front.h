/* The title screen and the clubhouse main menu. The platform draws the
 * screen's background picture (front_picture), then front_draw puts the
 * text, menu and any dialog over it. No platform headers. */
#ifndef FRONT_H
#define FRONT_H

#include "ui.h"

/* Background pictures. */
#define PIC_TITLE 1000
#define PIC_MENU  1100

/* Keys, from the platform's buttons. */
enum { KEY_NONE, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_SELECT, KEY_OTHER };

/* What the platform does after a key or tap. */
#define FR_REDRAW 1
#define FR_QUIT   2
/* Only the menu highlight moved, from oldSel to menu.sel: redrawing those two lines is enough. */
#define FR_LINES  4

enum { SCR_TITLE, SCR_MENU };

/* Main menu lines, in the order of MAIN_ITEMS. */
enum { MAIN_PLAY, MAIN_PRACTICE, MAIN_COURSES, MAIN_SETTINGS, MAIN_QUIT };

typedef struct {
	uint8_t screen;
	uint8_t dialog;          /* a dialog is open over the menu */
	uint8_t oldSel;
	UiMenu menu;
	UiDialog dlg;
} Front;

void SEC_UI front_init(Front *f);
uint16_t SEC_UI front_picture(const Front *f);
void SEC_UI front_draw(const Front *f, Surface *s);
int SEC_UI front_key(Front *f, uint8_t key);
int SEC_UI front_tap(Front *f, int16_t x, int16_t y);

#endif
