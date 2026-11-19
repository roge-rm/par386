/* The screen: picks the mode, makes the game's surface and the kept copy,
 * and copies pixels to the display. */
#ifndef SCREEN_H
#define SCREEN_H

#include <PalmOS.h>
#include "gfx.h"
#include "ui.h"

/* The game draws here, in pixels at the mode's depth (8 bits in 16-bit mode). */
extern Surface gS;

Err ScreenOpen(void);
void ScreenClose(void);
/* After a winDisplayChangedEvent or on opening the form: fits the form to
 * the display and makes the buffers again for its size, stepping down to
 * 8 bits, then 160 units square at 2 or 1 bit, if there's no room.
 * SCREEN_FAILED leaves gS.bits NULL and nothing can be drawn. */
#define SCREEN_SAME   0
#define SCREEN_NEW    1
#define SCREEN_FAILED 2
Int16 ScreenResize(void);
#ifdef DEBUG
/* Makes the next Make fail, once more for each call, to test the above. */
void ScreenFailNext(void);
#endif
/* The input area policy for the main form on devices that can close it. */
void ScreenFormInit(FormType *frm);
/* Copies a rectangle of gS (pixels) to the display, or all of it. */
void ScreenShow(const URect *d);
void ScreenShowAll(void);
/* The kept copy of gS: the last picture or the scene. ScreenKeep copies gS
 * there (making it if there's room) and returns false if there's none. */
Boolean ScreenKeep(void);
Surface *ScreenKept(void);
/* Copies a rectangle of the kept copy back to gS, or all of it. */
void ScreenRestore(const URect *d);
void ScreenRestoreAll(void);
/* Drops the kept copy, to free its memory. */
void ScreenDropKept(void);
/* The picture resource type for the mode: Pic8 at 8 and 16 bits, else Pic2. */
UInt32 ScreenPicType(void);
/* What the mode is, for the debug line: depth, scale and S for Sony HR,
 * 16 for 16-bit colour. */
void ScreenDescribe(char *t);

#endif
