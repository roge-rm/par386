/* The title screen and the clubhouse main menu. The platform draws the
 * screen's background picture (front_picture), then front_draw puts the
 * text, menu and any dialog over it. No platform headers. */
#ifndef FRONT_H
#define FRONT_H

#include "ui.h"
#include "rules.h"

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
/* A new round was picked: Front.players with Front.cpu, in Front.mode
 * (practice too). The platform loads the course and starts play. */
#define FR_PLAY   8
/* Continue round was picked: the platform loads the saved round. */
#define FR_CONTINUE 16
/* The sound setting changed: the platform saves it and plays by it. */
#define FR_SOUND  32
/* The computer golfers' shots setting (Front.quick) changed: the platform saves it. */
#define FR_CPU    64

/* The screens past the title are the menu board with a list. Games picks
 * the mode, players who plays each slot. */
enum { SCR_TITLE, SCR_MENU, SCR_PLAYERS, SCR_COURSES, SCR_SETTINGS, SCR_GAMES, SCR_PRACTICE };

/* A player slot nobody plays. Others are CPU_* (rules.h). */
#define SLOT_OFF 0xFF

/* The sound setting: everything, effects without the tunes and birds, or none. */
enum { SOUND_ON, SOUND_FX, SOUND_OFF, SOUND_MODES };

/* Courses the platform found, at most FRONT_COURSES, names up to 23
 * characters. The courses screen has room for 5 and Back; past that it
 * shows FRONT_PAGE a page with More and Back. */
#define FRONT_COURSES 12
#define FRONT_PAGE 4
#define FRONT_COURSE_LIST (5 * 24 + 10)

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
	uint8_t mode;            /* MODE_*, picked on the games or practice screen */
	uint8_t slot[RULES_PLAYERS];            /* who plays each slot: CPU_* or SLOT_OFF */
	uint8_t cpu[RULES_PLAYERS];             /* the round's players in order, without the slots off */
	uint8_t quick;           /* computer golfers' shots as a result on the map */
	uint8_t pending;         /* what OK on the new round dialog opens: SCR_GAMES or SCR_PRACTICE */
	uint8_t courses;         /* how many courses are installed */
	uint8_t course;          /* the one picked on the courses screen */
	uint8_t sound;           /* SOUND_* */
	char settings_list[40];  /* "Sound: On|CPU: Quick|Back" */
	char players_list[RULES_PLAYERS * 13 + 16 + 14];   /* "P1  Human|P2  CPU Fair|Tees: Medal|Tee off|Back" */
	uint8_t tee;             /* the tee set picked on the players screen, 0 the longest */
	uint8_t tee_count[FRONT_COURSES];       /* each course's tee sets, 0 if not known (a card course) */
	char tee_names[FRONT_COURSES][COURSE_MAX_TEES][TEE_NAME_MAX + 1];
	uint16_t rec_strokes[FRONT_COURSES][COURSE_MAX_TEES];   /* each course's record per tee set, 0 for none */
	int16_t rec_par[FRONT_COURSES][COURSE_MAX_TEES];
	char course_names[FRONT_COURSES][24];
	char course_list[FRONT_COURSE_LIST];    /* the page's names, More and Back: "A|B|More|Back" */
	uint8_t page;            /* the courses screen's page */
	uint8_t scenery[FRONT_COURSES];         /* each one's scenery, or SCENERY_UNKNOWN */
	UiMenu menu;
	UiDialog dlg;
} Front;

void SEC_MENU front_init(Front *f);
uint16_t SEC_MENU front_picture(const Front *f);
void SEC_MENU front_draw(const Front *f, Surface *s);
int SEC_MENU front_key(Front *f, uint8_t key);
int SEC_MENU front_tap(Front *f, int16_t x, int16_t y);
/* Opens a dialog with an OK button over the menu, such as when there's no course to play. */
void SEC_MENU front_message(Front *f, const char *title, const char *text);
/* Back to the clubhouse menu, with Continue round if saved isn't 0. */
void SEC_MENU front_clubhouse(Front *f, uint8_t saved);
/* Sets the installed courses' names (count of them), their scenery numbers
 * (or NULL if not known) and the one picked. */
void SEC_MENU front_courses(Front *f, const char *const *names, const uint8_t *scenery, uint8_t count, uint8_t pick);
/* The preview picture for the highlighted line of the courses screen, or 0. */
uint16_t SEC_MENU front_preview(const Front *f);
/* Draws one band of the preview picture into its box; -1 if it doesn't fit. */
int SEC_MENU front_preview_band(Surface *s, const uint8_t *d, uint32_t len);
/* Course i's tee set names, from its header, or NULL if it isn't known. */
void SEC_MENU front_course_tees(Front *f, uint8_t i, const Course *c);
/* Course i's record from tee set tee for the courses screen: strokes
 * against to_par; strokes 0 for none yet. */
void SEC_MENU front_record(Front *f, uint8_t i, uint8_t tee, uint16_t strokes, int16_t to_par);
/* The courses screen's record line for course i from the picked tee set,
 * "Medal 71 (-1)", or empty for none. t takes 20 characters. */
void SEC_MENU front_record_line(const Front *f, uint8_t i, char *t);
/* How many tee sets the picked course has and the name of set t. */
uint8_t SEC_MENU front_tee_count(const Front *f);
const char *SEC_MENU front_tee_name(const Front *f, uint8_t t);
/* The main menu action for a line of the menu. */
uint8_t SEC_MENU front_action(const Front *f, uint8_t line);

#endif
