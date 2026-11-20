/* Game sounds. The core asks for a sound with snd_play when something
 * happens; the platform takes the queued ids with snd_take, from its hook
 * or after each core call, and plays them (tones, tunes or samples). No
 * platform headers. */
#ifndef SOUND_H
#define SOUND_H

#include "section.h"
#include "fixed.h"

enum {
	SND_NONE,
	SND_SWING,          /* the meter starts */
	SND_CLICK,          /* the power click */
	SND_HIT_WOOD, SND_HIT_IRON, SND_HIT_PUTT,
	SND_LAND_FAIRWAY,   /* fairway, first cut, fringe, green or tee */
	SND_LAND_ROUGH,
	SND_LAND_SAND,
	SND_SPLASH,
	SND_CUP,            /* the ball drops in */
	SND_OOB,
	SND_HOLED,          /* tune: par */
	SND_BIRDIE,         /* tune: under par, except an ace */
	SND_BOGEY,          /* tune: bogey or worse */
	SND_MENU_MOVE, SND_MENU_SELECT,
	SND_LAND_PATH,
	SND_LIP,
	SND_MISHIT,
	SND_CLUB_CHANGE,    /* club or shot shape changed */
	SND_ACE,            /* tune: hole in one */
	SND_COUNT
};

/* Sounds the "effects only" setting leaves out. */
#define SND_IS_TUNE(id) ((id) == SND_HOLED || (id) == SND_BIRDIE || (id) == SND_BOGEY || (id) == SND_ACE)

#define SND_QUEUE 8     /* a power of 2 */

/* Queues a sound, then calls the hook if there is one, so the platform can
 * start it before any long work that follows (the shot's physics). When the
 * queue is full the oldest goes. */
void SEC_CORE snd_play(uint8_t id);
typedef void (*SndHook)(void);
void SEC_CORE snd_set_hook(SndHook fn);
/* The next queued sound, or SND_NONE. */
uint8_t SEC_CORE snd_take(void);
/* Birds on the tee view: on while aiming from the tee, off once the swing starts. */
void SEC_CORE snd_ambient(uint8_t on);
uint8_t SEC_CORE snd_ambient_on(void);

#ifndef __palmos__
/* Host builds keep every call for the tests: ids, with 0x80 + on for snd_ambient. */
#define SND_LOG_MAX 512
extern uint8_t snd_log[SND_LOG_MAX];
extern uint16_t snd_log_n;
#endif

#endif
