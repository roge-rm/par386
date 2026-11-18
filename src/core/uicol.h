/* UI colours. Each has an RGB for colour screens and a grey shade for grey
 * ones, 0 (black) to 6 (white); odd shades are a checker of the greys either
 * side. On 8-bit screens UI colour n is palette index n + 1, and
 * tools/mkart.py reads this table to put them there. On the IIIx both
 * middle greys show close to white, so grey text is black, on light greys. */
#ifndef UICOL_H
#define UICOL_H

#define UI_COLOURS \
	X(UIC_BLACK,      0,   0,   0, 0) \
	X(UIC_WHITE,    255, 255, 255, 6) \
	X(UIC_TEXT,     236, 226, 196, 0) \
	X(UIC_TEXT_HI,  255, 224,  90, 6) \
	X(UIC_SHADOW,    10,  40,  20, 0) \
	X(UIC_BAR,       34, 112,  60, 0) \
	X(UIC_BAR_LT,    60, 140,  80, 2) \
	X(UIC_BAR_DK,    10,  50,  24, 0) \
	X(UIC_PAPER,    244, 236, 212, 6) \
	X(UIC_PAPER_DK, 196, 182, 146, 4) \
	X(UIC_INK,       40,  30,  20, 0) \
	X(UIC_TITLE,     22,  82,  44, 0) \
	X(UIC_BRASS,    232, 184,  70, 4) \
	X(UIC_BRASS_LT, 255, 240, 170, 6) \
	X(UIC_BRASS_DK, 110,  64,  10, 0) \
	X(UIC_BALL_LT,  196, 196, 206, 4) \
	X(UIC_BALL_DK,  130, 130, 142, 2) \
	X(UIC_TEE,      220, 180, 110, 4) \
	X(UIC_TEE_DK,   120,  84,  40, 2)

enum {
#define X(name, r, g, b, grey) name,
	UI_COLOURS
#undef X
	UIC_COUNT
};

#endif
