/* AlphaSmart Dana's map and shot controls beside the panoramic scene. */
#ifndef DANA_H
#define DANA_H

#include "play.h"

#define DANA_SCREEN_W 560
#define DANA_RAIL_W 96
#define DANA_SCENE_W (DANA_SCREEN_W - 2 * DANA_RAIL_W)
#define DANA_METER_Y 99
#define DANA_METER_H 13

enum { DANA_LEFT = 1, DANA_RIGHT = 2, DANA_METER = 4 };

typedef struct {
	const Hole *hole;
	vec2 ball, target;
	RenderMap map;
	int16_t shown, marker_y;
	uint8_t marker_bytes[9];   /* up to three bytes on three rows under the ball */
	uint8_t marker_first, marker_count;
	uint8_t pin, map_ready, right_ready;
	uint8_t hole_number, tee, map_stroke, map_player;
	uint8_t wind;
	angle direction;
	uint8_t view, state, club, lie, shape, stroke, player, repeat;
	uint8_t card, board, card_player;
	uint8_t cpu, putt_tick;
	uint16_t distance, carry;
	int16_t meter, turn;
	const char *action;
	char msg1[21], msg2[21];
} DanaPanels;

void SEC_DANA dana_reset(DanaPanels *d);
/* Each pane is a 96 x 160 scale-1 surface. Returns the parts that changed. */
uint8_t SEC_DANA dana_draw(DanaPanels *d, Surface *left, Surface *right, const Play *p);
/* Coordinates are relative to the pane, in logical units. */
int SEC_DANA dana_tap(const DanaPanels *d, Play *p, uint8_t pane, int16_t x, int16_t y, uint32_t now);

#endif
