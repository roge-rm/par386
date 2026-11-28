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
#define PL_DIRTY  16    /* copy the dirty rectangle, and gdirty if it isn't empty, to the screen */
#define PL_ANIM   32    /* call play_frame again soon */
#define PL_CARD   64    /* put back the kept scene and draw the card or club picker */
#define PL_NEXT   128   /* load the round's hole and play_start it */
#define PL_TURN   256   /* another player's turn: show play_turn while the scene draws */
#define PL_PANEL  512   /* draw the map's panel again (play_status) and show it */

#define PLAY_CPU_MS 900 /* a computer golfer's result stays this long before play goes on */
#define PLAY_CPU_AIM_MS 600 /* and the scene shows this long before their swing */

#define PLAY_PATCHES 16 /* overlay patches drawn over the scene */

/* With the shot view on the map, a full shot goes from the scene to the map
 * this long after the hit, unless its flight shows for less than
 * PLAY_MAP_MIN_MS (chips stay in the scene). Putts always stay. The map is
 * drawn offscreen in slices while the ball flies in the scene
 * (play_map_wanted, play_map_prep); if it isn't ready before the ball
 * comes down the shot stays in the scene. */
#define PLAY_MAP_AFTER_MS 1000
#define PLAY_MAP_MIN_MS   1500
#define PLAY_UNDER 128  /* bytes kept under the ball on the map */
#define PLAY_FLIGHT_UNDER (18 * 18) /* largest saved overlay on a 320-wide scene */

/* The swing meter, in logical units along the panel's top row, and its timing. */
#define MTR_X      4    /* the bar's left */
#define MTR_W      152  /* its width: full power at the right end */
#define MTR_SNAP   16   /* the snap line, from the bar's left */
#define MTR_UP_MS  1000 /* the snap line to full power */
#define MTR_DOWN_MS 750 /* full power back to the snap line */
#define MTR_GREY_DOWN_MS 1200 /* full power back to snap on greyscale LCDs */
#define MTR_SNAP_MS 80  /* this far off the snap line past a 10 ms window is a full miss */
#define MTR_GRACE_MS 120 /* a press this soon after the meter turns back by itself is the power press at full */

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
	uint32_t aim_time;           /* when the crosshair first drew */
	uint8_t aim_mark;            /* opacity step, or waiting for the first draw */
	uint8_t redraw;             /* R_* bits */
	uint8_t npatch;
	URect patch[PLAY_PATCHES];  /* overlay pixels drawn over the scene */
	URect dirty;                /* pixels to copy to the screen */
	URect gdirty;               /* the golfer's when his pose changed, kept apart so the two don't make one big copy */
	/* the swing */
	uint32_t t0, t1;            /* when the swing started and when it turned */
	int16_t turn;               /* the marker where it turned, 1/16 units from the bar's left */
	int16_t mpos;               /* the marker now */
	int16_t mshown;             /* the marker's column as last drawn, logical units */
	uint8_t autoturn;           /* the meter reached full with no power press and turned back: 1 drawn, 2 on the screen since tg */
	uint32_t tg;                /* when the full marker was drawn, then shown (play_shown) */
	uint8_t mfirst;             /* the meter's first two frames (the meter, then the golfer's backswing) still to show, for play_shown */
	fixed power, snap;
	fixed range;                /* putter: yards at full power on a flat green */
	uint8_t putt_tick;          /* cup pace mark in logical units from the snap line */
	uint8_t mishit;
	uint8_t score;              /* strokes for the hole, once it's done */
	/* the flight */
	uint32_t tf;
	int16_t shown;              /* path point drawn last, -1 for none */
	uint8_t have_shot;
	uint8_t flight_sounds;      /* collision cues already played for this shot */
	uint8_t result_pending;     /* the platform is preparing the next view */
	char msg1[21], msg2[21];
	/* the round, or 0 for a hole played alone */
	Round *round;
	uint8_t cur;                /* the player whose shot it is */
	uint8_t card, page, csel;   /* the scorecard's CARD_* mode, its page and button */
	uint8_t stat_player;
	uint8_t card_from;          /* the view it was opened from, PLAY_SCENE or PLAY_MAP */
	Shot shot;
	Surface geo;                /* the surface's size and scale as last drawn, for taps */
	vec2 target;                /* where the aiming map's marker sits on the hole */
	fixed target_range;         /* distance to keep while turning the target with the keys */
	uint8_t target_set;         /* the player picked this point on the map */
	uint8_t map_near;
	vec2 map_focus;              /* the local map's focus stays put while picking a point */
	RenderGolfer *golf;         /* the golfer in the scene, set by the platform; 0 for none */
	/* computer golfers */
	uint8_t cpu;                /* the skill of the player whose shot it is, CPU_HUMAN for a person */
	uint8_t left_layout;
	uint8_t quick;              /* their shots show as a result on the map, not a scene and flight; the platform sets it */
	uint8_t wait;               /* tw is set on the next frame */
	uint8_t applied;            /* play_settle has already counted the result being shown */
	uint32_t tw;                /* when their wait started */
	uint8_t board;              /* the card view shows the tournament leaderboard */
	/* the shot on the map */
	uint8_t mapview;            /* full shots go over to the map in flight; the platform sets it */
	uint8_t onmap;              /* this shot is on the map (view PLAY_MAP) */
	uint8_t mprep;              /* the map for this shot: 0 not started, 1 drawing (mrow rows done, then -1 to -3 the panel's face, its words and the trail), 2 ready, 3 too late */
	int16_t mrow;
	int16_t mdrawn;             /* the last path point of the trail drawn on the map */
	uint16_t mlive;             /* the readout last put on the panel */
	RenderMap mg;               /* where the current map put things */
	URect under;                /* the pixels under the ball on the map, kept in ubits */
	uint8_t ubits[PLAY_UNDER];
	uint16_t flight_saved;
	uint8_t flight_under[PLAY_FLIGHT_UNDER]; /* aim crosshair or flight overlays without a kept scene */
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
int SEC_BALL play_tap(Play *p, int16_t x, int16_t y, uint32_t now);
/* Moves the meter and the ball on to now and draws what changed over the
 * scene in s, taking old overlays off from bg (the kept scene, or 0 if
 * there's none). The golfer's pose follows the swing in s and bg. Call
 * after PL_RENDER or PL_SCENE, and while PL_ANIM is set. */
int SEC_UI play_frame(Play *p, Surface *s, Surface *bg, uint32_t now);
/* While the result holds, show its next action once the draw ahead is
 * ready or there's no draw ahead for this shot. */
void SEC_UI play_result_pending(Play *p, uint8_t pending);
/* Call after the frame play_frame drew is on the screen. The swing or
 * putting meter's first frame draws only the meter and the second the
 * golfer's backswing; once each shows, the meter's clock is set back so
 * the marker on the screen is where it is now, so it starts empty however
 * long they took to show and never steps back. The grace for a late power
 * press runs from when the full marker shows. */
void SEC_UI play_shown(Play *p, uint32_t now);
/* The panel on the map: target and club carry while aiming, the computer
 * golfer's shot, or the live carry and total on the flight map. */
void SEC_MENU play_status(Surface *s, const Play *p, const char *info);
/* The panel while the scene is drawn for a new turn or hole: the hole, and
 * who plays. */
void SEC_MENU play_turn(Surface *s, const Play *p);
/* The scorecard, leaderboard or club picker, over whatever is on s. */
void SEC_RENDER play_card(Surface *s, Play *p);
/* The hole map with the aim line, target and last shot's path;
 * or, for a shot on the map, the map framed on the shot with its trail so
 * far and the ball. */
void SEC_MENU play_map(Surface *s, Play *p);
/* True while a shot in the scene will go over to the map: the platform
 * then calls play_map_prep after each frame. */
int SEC_UI play_map_wanted(const Play *p);
/* Draws the next rows (at least 1) of the shot's map into m, a surface the
 * size of the scene's that isn't shown and keeps what's drawn until the
 * switch; once it's all there, the panel, then the trail so far on each
 * call. Returns 1 when the map is ready: on the switch (PL_MAP) the
 * platform fades from the scene to m, calling play_frame on m for each
 * step, and then copies m to the scene's surface. */
int SEC_UI play_map_prep(Surface *m, Play *p, int16_t rows);
/* From the swing on (the shot's flight and where it stops are worked out
 * then) until its result is taken: sets *v to the view the next shot will
 * be drawn from (the scene, or the green view for the putter) and returns
 * 1, if that's known now: the same player's next shot, or a person's
 * turn. Returns 0 for the end of the hole, a computer golfer's turn,
 * practice, or with no shot flying or showing. The platform can draw it ahead
 * (render_scene_begin, render_green_begin) and use it if rv matches it
 * once PL_RENDER comes. */
int SEC_MENU play_next_view(const Play *p, RenderView *v);
/* One press of a select key counts once. Some devices send two codes
 * for one press (the TX centre sends vchrTxCenter and vchrRockerCenter),
 * repeat a held key, or a worn switch bounces. A select press counts
 * unless it repeats, comes from another code within GATE_PAIR_MS of the
 * one that counted, comes again from the same code while that one is
 * held (a release was promised and hasn't come, on a device that has sent
 * releases before; for at most GATE_HELD_MS), or within GATE_BOUNCE_MS of
 * it. A real second press, however quick, counts. */
#define GATE_PAIR_MS   200
#define GATE_BOUNCE_MS 40
#define GATE_HELD_MS   1500
typedef struct {
	uint16_t code;              /* the character of the press that counted last, 0 for none */
	uint16_t key;               /* and its key code */
	uint8_t held;               /* it's down and its release will come */
	uint8_t ups;                /* releases have come before, so held can be trusted */
	uint32_t t;                 /* when it counted */
} KeyGate;
/* A select press of code (its character, non-zero) and key (its key
 * code), with repeat set for an auto-repeat and up set if a release will be
 * reported. Returns 1 if it counts. */
int SEC_CORE gate_press(KeyGate *g, uint16_t code, uint16_t key, uint8_t repeat, uint8_t up, uint32_t now);
/* A key was let go: it's the held one if its character or key code matches. */
void SEC_CORE gate_release(KeyGate *g, uint16_t code, uint16_t key);
/* Key events were thrown away (a flush or an alert's own event loop), so a
 * release may be lost: nothing counts as held. A key still down only sends
 * auto-repeats, which are dropped anyway. */
void SEC_CORE gate_flush(KeyGate *g);
/* Writes v in decimal at t, returns the length. */
int16_t SEC_UI play_num(char *t, uint32_t v);
int SEC_MENU play_dana_clubs(Play *p);
int SEC_MENU play_dana_aim(Play *p, vec2 point);
const char *SEC_MENU play_dana_action(const Play *p);
uint16_t SEC_MENU play_dana_distance(const Play *p);

#endif
