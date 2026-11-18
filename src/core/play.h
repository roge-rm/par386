/* Playing a hole: the view (the scene from behind the ball or the hole
 * map), aiming and picking a club, the 3-click swing meter, the ball's
 * flight drawn over the scene, the result and the status strip. The
 * platform loads the hole, keeps the surface grid, draws the scene when
 * asked and keeps a copy of it, and passes in the time. No platform headers.
 *
 * Times are in ms from any start. Overlays (the aim marker, the ball, the
 * meter and the strip) are drawn into the surface over the scene and taken
 * off again by copying back from the kept copy. */
#ifndef PLAY_H
#define PLAY_H

#include "render.h"
#include "front.h"
#include "ball.h"

enum { PLAY_SCENE, PLAY_MAP };

/* Where the shot is at. */
enum { PS_AIM, PS_UP, PS_DOWN, PS_FLY, PS_RESULT, PS_GREEN };

/* What the platform does after a key, tap or frame. */
#define PL_RENDER 1     /* draw the scene for rv again and keep it, then call play_frame */
#define PL_SCENE  2     /* put back the kept scene, then call play_frame */
#define PL_MAP    4     /* draw the map */
#define PL_BACK   8     /* back to the clubhouse */
#define PL_DIRTY  16    /* copy the dirty rectangle to the screen */
#define PL_ANIM   32    /* call play_frame again soon */

#define PLAY_SPOTS 3    /* camera spots the debug keys step through */
#define PLAY_PATCHES 8  /* overlay patches drawn over the scene */

/* The swing meter, in logical units along the status strip, and its timing. */
#define MTR_X      4    /* the bar's left */
#define MTR_W      152  /* its width: full power at the right end */
#define MTR_SNAP   16   /* the snap line, from the bar's left */
#define MTR_UP_MS  1000 /* the snap line to full power */
#define MTR_DOWN_MS 750 /* full power back to the snap line */
#define MTR_SNAP_MS 80  /* this far off the snap line past a 10 ms window is a full miss */

typedef struct {
	RenderView rv;              /* the scene as drawn: the ball, the view's aim and the pin */
	RenderCam cam;
	uint8_t cam_ok;
	uint8_t view, state, spot;
	uint8_t club, lie, stroke;
	uint8_t wind_mph;
	angle wind_dir;
	angle aim;                  /* the shot's aim; the scene was drawn at rv.aim */
	uint8_t redraw;             /* R_* bits */
	uint8_t npatch;
	URect patch[PLAY_PATCHES];  /* overlay pixels drawn over the scene */
	URect dirty;                /* pixels to copy to the screen */
	/* the swing */
	uint32_t t0, t1;            /* when the swing started and when it turned */
	int16_t turn;               /* the marker where it turned, 1/16 units from the bar's left */
	int16_t mpos;               /* the marker now */
	int16_t mshown;             /* the marker's column as last drawn, logical units */
	fixed power, snap;
	uint8_t mishit;
	/* the flight */
	uint32_t tf;
	int16_t shown;              /* path point drawn last, -1 for none */
	uint8_t have_shot;
	char msg1[21], msg2[21];
	Shot shot;
} Play;

/* seed sets the wind, as from the time the round started. */
void SEC_UI play_init(Play *p, const Hole *h, const SurfaceGrid *g, uint32_t seed);
/* Debug: puts the ball at camera spot n: the tee, then the two render-test spots on the fairway. */
void SEC_UI play_spot(Play *p, uint8_t n);
int SEC_UI play_key(Play *p, uint8_t key, uint32_t now);
/* x and y in logical units. */
int SEC_UI play_tap(Play *p, int16_t x, int16_t y, uint32_t now);
/* Moves the meter and the ball on to now and draws what changed over the
 * scene in s, taking old overlays off from bg (the kept scene, or 0 if
 * there's none). Call after PL_RENDER or PL_SCENE, and while PL_ANIM is set. */
int SEC_UI play_frame(Play *p, Surface *s, const Surface *bg, uint32_t now);
/* The status strip on the map: hole, par and yards, and a second line of
 * text if info isn't 0. */
void SEC_UI play_status(Surface *s, const Play *p, const char *info);
/* The hole map with the yardage by the aim line and the last shot's path. */
void SEC_UI play_map(Surface *s, const Play *p);
/* Writes v in decimal at t, returns the length. */
int16_t SEC_UI play_num(char *t, uint32_t v);

#endif
