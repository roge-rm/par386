/* The scorecard: a dialog with each hole's par and every player's strokes,
 * the out, in and total and the score against par, and the mode's
 * standing (skins won, the match in the title). Nine holes to a page; a
 * course of more than 9 holes has a button for the other nine. Also the
 * tournament leaderboard. No platform headers. */
#ifndef CARD_H
#define CARD_H

#include "ui.h"
#include "rules.h"

/* What the first button does: back to play, on to the next hole, or the round's done. */
enum { CARD_VIEW, CARD_HOLE, CARD_FINAL };

#define CARD_ROW 9    /* logical units per line */

/* Pages of 9 holes. */
uint8_t SEC_MENU card_pages(const Round *r);
/* Lays the card out in d: the box, title and buttons. page 0 is the front nine. */
void SEC_MENU card_layout(UiDialog *d, const Round *r, uint8_t mode, uint8_t page);
/* Draws it, with button sel picked. */
void SEC_MENU card_draw(Surface *s, const Round *r, uint8_t mode, uint8_t page, uint8_t sel);
/* The button under (x, y), or -1. */
int SEC_MENU card_hit(const Round *r, uint8_t mode, uint8_t page, int16_t x, int16_t y);
/* How many buttons the card has, and which one opens the tournament
 * leaderboard (0xFF for none). */
uint8_t SEC_MENU card_buttons(const Round *r, uint8_t mode);
uint8_t SEC_MENU card_board_button(const Round *r, uint8_t mode);

/* The tournament leaderboard: every player and the field, to par over the
 * finished holes, best first. Its first button does what the card's first
 * does in mode, the second opens the card. */
void SEC_MENU board_layout(UiDialog *d, const Round *r, uint8_t mode, uint8_t lines);
void SEC_MENU board_draw(Surface *s, const Round *r, uint8_t mode, uint8_t sel);
int SEC_MENU board_hit(const Round *r, uint8_t mode, int16_t x, int16_t y);
uint8_t SEC_MENU board_buttons(const Round *r, uint8_t mode);

#endif
