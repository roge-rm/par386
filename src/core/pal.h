/* Made by tools/mkpal.py, don't edit. Where each ramp of the master
 * palette starts and how many entries it has, dark to light. 0 is white,
 * 255 black and UI colour n is index n + 1. */
#ifndef PAL_H
#define PAL_H

#define PAL_RAMP_N 8    /* levels in each terrain ramp, in surface order from PAL_TERRAIN */
#define PAL_WHITE 0
#define PAL_BLACK 255
#define PAL_UI 1
#define PAL_UI_N 19
#define PAL_TERRAIN 20
#define PAL_TERRAIN_N 88
#define PAL_ROUGH 20
#define PAL_ROUGH_N 8
#define PAL_FIRST_CUT 28
#define PAL_FIRST_CUT_N 8
#define PAL_FAIRWAY 36
#define PAL_FAIRWAY_N 8
#define PAL_DEEP_ROUGH 44
#define PAL_DEEP_ROUGH_N 8
#define PAL_GREEN 52
#define PAL_GREEN_N 8
#define PAL_FRINGE 60
#define PAL_FRINGE_N 8
#define PAL_SAND 68
#define PAL_SAND_N 8
#define PAL_WATER 76
#define PAL_WATER_N 8
#define PAL_PATH 84
#define PAL_PATH_N 8
#define PAL_OOB 92
#define PAL_OOB_N 8
#define PAL_TEE 100
#define PAL_TEE_N 8
#define PAL_HAZE 108
#define PAL_HAZE_N 8
#define PAL_SKY 116
#define PAL_SKY_N 16
#define PAL_FAR 132
#define PAL_FAR_N 8
#define PAL_TREE 140
#define PAL_TREE_N 12
#define PAL_PINE 152
#define PAL_PINE_N 10
#define PAL_BARK 162
#define PAL_BARK_N 6
#define PAL_ROCK 168
#define PAL_ROCK_N 8
#define PAL_WALL 176
#define PAL_WALL_N 8
#define PAL_SKIN 184
#define PAL_SKIN_N 6
#define PAL_SHIRT 190
#define PAL_SHIRT_N 6
#define PAL_TROUSERS 196
#define PAL_TROUSERS_N 5
#define PAL_CAP 201
#define PAL_CAP_N 4
#define PAL_SHOES 205
#define PAL_SHOES_N 3
#define PAL_CLUB 208
#define PAL_CLUB_N 4
#define PAL_FLAG 212
#define PAL_FLAG_N 4
#define PAL_GREY 216
#define PAL_GREY_N 8
#define PAL_WOOD 224
#define PAL_WOOD_N 6
#define PAL_RED 230
#define PAL_RED_N 6
#define PAL_GOLD 236
#define PAL_GOLD_N 6
#define PAL_NAVY 242
#define PAL_NAVY_N 6
#define PAL_BLOSSOM 248
#define PAL_BLOSSOM_N 4
#define PAL_SPARE 252
#define PAL_SPARE_N 3

#endif
