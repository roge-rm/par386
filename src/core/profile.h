#ifndef PROFILE_H
#define PROFILE_H

#include <stdint.h>

#define PLAYER_NAME_MAX 8
#define PLAYER_DEFAULT_NAME "Player 1"

enum { GOLFER_MALE, GOLFER_FEMALE, GOLFER_MODELS };
enum { SKIN_LIGHT, SKIN_MEDIUM, SKIN_DARK, SKIN_COLOURS };
enum { OUTFIT_DEFAULT, OUTFIT_NAVY, OUTFIT_GREEN, OUTFIT_RED, OUTFIT_GOLD,
       OUTFIT_WHITE, OUTFIT_CHARCOAL, OUTFIT_COLOURS };

typedef struct {
	char name[PLAYER_NAME_MAX + 1];
	uint8_t golfer, skin, left;
	uint8_t shoes, pants, shirt, hat;
} PlayerProfile;

#endif
