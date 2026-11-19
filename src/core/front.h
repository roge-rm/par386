/* The title screen and the clubhouse main menu. The platform draws the
 * screen's background picture (front_picture), then front_draw puts the
 * text, menu and any dialog over it. No platform headers. */
#ifndef FRONT_H
#define FRONT_H

#include "ui.h"

/* Background pictures. */
#define PIC_TITLE 1000
#define PIC_MENU  1100
/* Course previews, one picture per scenery number: PIC_PREVIEW + 10 * scenery
 * (tools/mkpreview.py), shown on the courses screen in the box below. */
#define PIC_PREVIEW 1200
#define PREVIEW_X 28
#define PREVIEW_Y 110
#define PREVIEW_W 104
#define PREVIEW_H 32
/* A course's scenery when the platform can't tell it: no preview. */
#define SCENERY_UNKNOWN 0xFF

/* Keys, from the platform's buttons. KEY_CARD opens the scorecard while
 * playing (c or s), KEY_MAP the map and KEY_SHAPE picks the next shot
 * shape. */
enum { KEY_NONE, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_SELECT, KEY_OTHER, KEY_BACK, KEY_CARD, KEY_MAP, KEY_SHAPE };

/* What the platform does after a key or tap. */
#define FR_REDRAW 1
#define FR_QUIT   2
/* Only the menu highlight moved, from oldSel to menu.sel: redrawing those two lines is enough. */
#define FR_LINES  4
/* A new round for Front.players was picked: the platform loads the course and starts play. */
#define FR_PLAY   8
/* Continue round was picked: the platform loads the saved round. */
#define FR_CONTINUE 16
/* The sound setting changed: the platform saves it and plays by it. */
#define FR_SOUND  32

/* The players and courses screens are the menu board with a list. */
enum { SCR_TITLE, SCR_MENU, SCR_PLAYERS, SCR_COURSES, SCR_SETTINGS };

/* The sound setting: everything, effects without the tunes and birds, or none. */
enum { SOUND_ON, SOUND_FX, SOUND_OFF, SOUND_MODES };

/* Courses the platform found, at most FRONT_COURSES, names up to 23 characters. */
#define FRONT_COURSES 5
#define FRONT_COURSE_LIST (FRONT_COURSES * 24 + 5)

/* Main menu actions. With a saved round Continue round comes first and the
 * rest move down a line. */
enum { MAIN_PLAY, MAIN_PRACTICE, MAIN_COURSES, MAIN_SETTINGS, MAIN_QUIT, MAIN_CONTINUE };

/* Dialogs: a message with OK, or asking before a new round replaces the saved one. */
enum { DLG_NONE, DLG_MESSAGE, DLG_NEW_ROUND };

typedef struct {
	uint8_t screen;
	uint8_t dialog;          /* DLG_*: a dialog is open over the menu */
	uint8_t oldSel;
	uint8_t saved;           /* there's a round to continue */
	uint8_t players;         /* picked on the players screen */
	uint8_t courses;         /* how many courses are installed */
	uint8_t course;          /* the one picked on the courses screen */
	uint8_t sound;           /* SOUND_* */
	char settings_list[24];  /* "Sound: On|Back" */
	char course_list[FRONT_COURSE_LIST];    /* their names and Back, "A|B|Back" */
	uint8_t scenery[FRONT_COURSES];         /* each one's scenery, or SCENERY_UNKNOWN */
	UiMenu menu;
	UiDialog dlg;
} Front;

void SEC_UI front_init(Front *f);
uint16_t SEC_UI front_picture(const Front *f);
void SEC_UI front_draw(const Front *f, Surface *s);
int SEC_UI front_key(Front *f, uint8_t key);
int SEC_UI front_tap(Front *f, int16_t x, int16_t y);
/* Opens a dialog with an OK button over the menu, such as when there's no course to play. */
void SEC_UI front_message(Front *f, const char *title, const char *text);
/* Back to the clubhouse menu, with Continue round if saved isn't 0. */
void SEC_UI front_clubhouse(Front *f, uint8_t saved);
/* Sets the installed courses' names (count of them), their scenery numbers
 * (or NULL if not known) and the one picked. */
void SEC_UI front_courses(Front *f, const char *const *names, const uint8_t *scenery, uint8_t count, uint8_t pick);
/* The preview picture for the highlighted line of the courses screen, or 0. */
uint16_t SEC_UI front_preview(const Front *f);
/* Draws one band of the preview picture into its box; -1 if it doesn't fit. */
int SEC_UI front_preview_band(Surface *s, const uint8_t *d, uint32_t len);
/* The main menu action for a line of the menu. */
uint8_t SEC_UI front_action(const Front *f, uint8_t line);

#endif
