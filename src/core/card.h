/* The scorecard: a dialog with each hole's par and every player's strokes,
 * the out, in and total and the score against par. Nine holes to a page;
 * a course of more than 9 holes has a button for the other nine. No
 * platform headers. */
#ifndef CARD_H
#define CARD_H

#include "ui.h"
#include "rules.h"

/* What the first button does: back to play, on to the next hole, or the round's done. */
enum { CARD_VIEW, CARD_HOLE, CARD_FINAL };

#define CARD_ROW 9    /* logical units per line */

/* Pages of 9 holes. */
uint8_t SEC_UI card_pages(const Round *r);
/* Lays the card out in d: the box, title and buttons. page 0 is the front nine. */
void SEC_UI card_layout(UiDialog *d, const Round *r, uint8_t mode, uint8_t page);
/* Draws it, with button sel picked. */
void SEC_UI card_draw(Surface *s, const Round *r, uint8_t mode, uint8_t page, uint8_t sel);
/* The button under (x, y), or -1. */
int SEC_UI card_hit(const Round *r, uint8_t mode, uint8_t page, int16_t x, int16_t y);

#endif
