/* Playing a hole: which view is up (the scene from behind the ball or the
 * hole map), the keys and taps for it and the status strip. The platform
 * loads the hole, keeps the surface grid and draws the scene when asked.
 * No platform headers. */
#ifndef PLAY_H
#define PLAY_H

#include "render.h"
#include "front.h"

enum { PLAY_SCENE, PLAY_MAP };

/* What the platform does after a key or tap. */
#define PL_RENDER 1     /* draw the scene again (the camera moved) */
#define PL_SCENE  2     /* show the scene as last drawn */
#define PL_MAP    4     /* draw the map */
#define PL_BACK   8     /* back to the clubhouse */

#define PLAY_SPOTS 3    /* camera spots the debug keys step through */

typedef struct {
	RenderView rv;
	uint8_t view;
	uint8_t spot;
} Play;

void SEC_UI play_init(Play *p, const Hole *h, const SurfaceGrid *g);
/* Puts the ball at camera spot n: the tee, then the two render-test spots on the fairway. */
void SEC_UI play_spot(Play *p, uint8_t n);
int SEC_UI play_key(Play *p, uint8_t key);
int SEC_UI play_tap(Play *p, int16_t x, int16_t y);
/* The status strip: hole, par and yards, and a second line of text if info isn't 0. */
void SEC_UI play_status(Surface *s, const Play *p, const char *info);
/* The hole map with the yardage by the aim line. */
void SEC_UI play_map(Surface *s, const Play *p);
/* Writes v in decimal at t, returns the length. */
int16_t SEC_UI play_num(char *t, uint32_t v);

#endif
