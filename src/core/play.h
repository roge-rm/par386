/* Playing a hole: the view (the scene from behind the ball, the green view
 * for putting or the hole map), aiming and picking a club, the 3-click swing
 * meter and the 2-click putting meter, the ball's flight or roll drawn over
 * the view, the result, holing out and the panel below the view. The
 * platform loads the hole, keeps the surface grid, draws the scene when
 * asked and keeps a copy of it, and passes in the time. No platform headers.
 *
 * Times are in ms from any start. Overlays (the aim marker, the ball, the
 * meter and the panel) are drawn into the surface over the scene and taken
 * off again by copying back from the kept copy. */
#ifndef PLAY_H
#define PLAY_H

#include "render.h"
#include "front.h"
#include "ball.h"
#include "rules.h"
#include "card.h"

enum { PLAY_SCENE, PLAY_MAP, PLAY_CARD };

/* Where the shot is at. */
enum { PS_AIM, PS_UP, PS_DOWN, PS_FLY, PS_RESULT, PS_DONE };

/* What the platform does after a key, tap or frame. */
#define PL_RENDER 1     /* draw the scene for rv again and keep it, then call play_frame */
#define PL_SCENE  2     /* put back the kept scene, then call play_frame */
#define PL_MAP    4     /* draw the map */
#define PL_BACK   8     /* back to the clubhouse */
#define PL_DIRTY  16    /* copy the dirty rectangle to the screen */
#define PL_ANIM   32    /* call play_frame again soon */
#define PL_CARD   64    /* put back the kept scene and draw the scorecard over it (play_card) */
#define PL_NEXT   128   /* load the round's hole and play_start it */
#define PL_TURN   256   /* another player's turn: show play_turn while the scene draws */
#define PL_PANEL  512   /* draw the map's panel again (play_status) and show it */

#define PLAY_CPU_MS 900 /* a computer golfer's result stays this long before play goes on */
#define PLAY_CPU_AIM_MS 600 /* and the scene shows this long before their swing */

#define PLAY_PATCHES 16 /* overlay patches drawn over the scene */

/* With the shot view on the map, a full shot goes from the scene to the map
 * this long after the hit, unless its flight shows for less than
 * PLAY_MAP_MIN_MS (chips stay in the scene). Putts always stay. */
#define PLAY_MAP_AFTER_MS 1000
#define PLAY_MAP_MIN_MS   1500
#define PLAY_UNDER 128  /* bytes kept under the ball on the map */
#define PLAY_MAP_GAP_MS 60  /* a first map frame later than this after the switch holds the flight back by the rest */

/* The swing meter, in logical units along the panel's top row, and its timing. */
#define MTR_X      4    /* the bar's left */
#define MTR_W      152  /* its width: full power at the right end */
#define MTR_SNAP   16   /* the snap line, from the bar's left */
#define MTR_UP_MS  1000 /* the snap line to full power */
#define MTR_DOWN_MS 750 /* full power back to the snap line */
#define MTR_SNAP_MS 80  /* this far off the snap line past a 10 ms window is a full miss */

/* A ball left this many feet from the cup on the green, as the panel
 * shows it (to the nearest foot), is given: the next putt counts and the
 * hole ends. */
#define PLAY_GIMME_FT 2

typedef struct {
	RenderView rv;              /* the scene as drawn: the ball, the view's aim and the pin */
	RenderCam cam;
	uint8_t cam_ok;
	uint8_t view, state;
	uint8_t club, lie, stroke;
	uint8_t shape;              /* SHAPE_*, back to normal for each shot */
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
	fixed range;                /* putter: yards at full power on a flat green */
	uint8_t mishit;
	uint8_t score;              /* strokes for the hole, once it's done */
	/* the flight */
	uint32_t tf;
	int16_t shown;              /* path point drawn last, -1 for none */
	uint8_t have_shot;
	char msg1[21], msg2[21];
	/* the round, or 0 for a hole played alone */
	Round *round;
	uint8_t cur;                /* the player whose shot it is */
	uint8_t card, page, csel;   /* the scorecard's CARD_* mode, its page and button */
	uint8_t card_from;          /* the view it was opened from, PLAY_SCENE or PLAY_MAP */
	Shot shot;
	Surface geo;                /* the surface's size and scale as last drawn, for taps */
	RenderGolfer *golf;         /* the golfer in the scene, set by the platform; 0 for none */
	/* computer golfers */
	uint8_t cpu;                /* the skill of the player whose shot it is, CPU_HUMAN for a person */
	uint8_t quick;              /* their shots show as a result on the map, not a scene and flight; the platform sets it */
	uint8_t wait;               /* tw is set on the next frame */
	uint8_t applied;            /* play_settle has already counted the result being shown */
	uint32_t tw;                /* when their wait started */
	uint8_t board;              /* the card view shows the tournament leaderboard */
	/* the shot on the map */
	uint8_t mapview;            /* full shots go over to the map in flight; the platform sets it */
	uint8_t onmap;              /* this shot is on the map (view PLAY_MAP) */
	uint8_t mpause;             /* the next map frame takes the time since the switch (tmap) off the flight */
	uint32_t tmap;              /* when the shot went over to the map */
	int16_t mdrawn;             /* the last path point of the trail drawn on the map */
	uint16_t mlive;             /* the readout last put on the panel */
	RenderMap mg;               /* where the map put things */
	URect under;                /* the pixels under the ball on the map, kept in ubits */
	uint8_t ubits[PLAY_UNDER];
} Play;

/* A hole on its own with one player, from the tee. seed sets the wind, as
 * from the time the round started. */
void SEC_UI play_init(Play *p, const Hole *h, const SurfaceGrid *g, uint32_t seed);
/* The round's hole h: the player whose turn it is, from where their ball
 * lies. The round mustn't be over or have everyone holed. With a computer
 * golfer to play and quick set, the view is the map with their shot's
 * result (call play_map and play_status, then play_frame while PL_ANIM). */
void SEC_UI play_start(Play *p, const Hole *h, const SurfaceGrid *g, Round *r);
/* Before the round is saved and play left: a shot in the air counts where
 * it ends up, and a finished hole moves the round on to the next. */
void SEC_UI play_settle(Play *p);
/* The surface changed size: the camera is worked out again and everything
 * drawn again on the next frame. */
void SEC_UI play_resized(Play *p);
#ifdef DEBUG
/* Debug builds: puts the ball on the green 7 yd short of the pin, for
 * screenshots of the green view. Returns PL_RENDER. */
int SEC_UI play_debug_green(Play *p);
#endif
int SEC_UI play_key(Play *p, uint8_t key, uint32_t now);
/* x and y in logical units. */
int SEC_UI play_tap(Play *p, int16_t x, int16_t y, uint32_t now);
/* Moves the meter and the ball on to now and draws what changed over the
 * scene in s, taking old overlays off from bg (the kept scene, or 0 if
 * there's none). The golfer's pose follows the swing in s and bg. Call
 * after PL_RENDER or PL_SCENE, and while PL_ANIM is set. */
int SEC_UI play_frame(Play *p, Surface *s, Surface *bg, uint32_t now);
/* The panel on the map: hole, par and yards, and below them info if it
 * isn't 0 (the hole's name if it is), with the Card button in a round; or
 * a computer golfer's shot; or for a shot on the map the club, the carry
 * or total so far and the wind, then the result. */
void SEC_UI play_status(Surface *s, const Play *p, const char *info);
/* The panel while the scene is drawn for a new turn or hole: the hole, and
 * who plays. */
void SEC_UI play_turn(Surface *s, const Play *p);
/* The scorecard or the tournament leaderboard, over whatever is on s,
 * with the panel showing the hole. */
void SEC_UI play_card(Surface *s, const Play *p);
/* The hole map with the yardage by the aim line and the last shot's path;
 * or, for a shot on the map, the map framed on the shot with its trail so
 * far and the ball. */
void SEC_UI play_map(Surface *s, Play *p);
/* Writes v in decimal at t, returns the length. */
int16_t SEC_UI play_num(char *t, uint32_t v);

#endif
