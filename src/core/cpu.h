/* Computer golfers: a shot planned from what a person playing sees (the
 * lie, the yards, the wind, the hole map and the green's slope), then
 * swung with errors that depend on the skill level, through the same
 * physics. The same view, skill and seed always give the same shot. No
 * platform headers. */
#ifndef CPU_H
#define CPU_H

#include "ball.h"

typedef struct {
	const Hole *hole;
	const SurfaceGrid *grid;
	vec2 ball, pin;
	vec2 to;            /* where the shot is aimed first: the hole's aim from the tee, else the pin */
	uint8_t lie;
	uint8_t wind_mph;
	angle wind_dir;     /* the way it blows */
	fixed range;        /* the putter's full meter in yards, as the panel's meter shows it */
} CpuView;

typedef struct {
	angle aim;
	fixed power, snap;
	uint8_t club, shape;
} CpuShot;

/* skill is CPU_EASY to CPU_PRO (rules.h); on the green and the fringe it's the putter. */
void SEC_CORE cpu_shot(const CpuView *v, uint8_t skill, uint32_t seed, CpuShot *out);

#endif
