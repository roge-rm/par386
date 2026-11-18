/* Palm platform code: launch handling, the screen, input and the event loop.
 * The game draws every screen itself into an offscreen window at the
 * screen's own depth (core gfx.c and ui.c) and copies it to the display. */
#define ALLOW_ACCESS_TO_INTERNALS_OF_WINDOWS
#include <PalmOS.h>
#include "resource.h"
#include "front.h"
#include "play.h"

#define kMinRomVersion sysMakeROMVersion(3, 1, 0, sysROMStageRelease, 0)
#define kRom35 sysMakeROMVersion(3, 5, 0, sysROMStageRelease, 0)

/* palmOne 5-way: keyCode holds the held bits, the change bits sit 8 above. */
#define vchrNavChange  0x0503
#define kNavUp         0x0001
#define kNavDown       0x0002
#define kNavLeft       0x0004
#define kNavRight      0x0008
#define kNavSelect     0x0010
#define vchrTxCenter   0x013D

static WinHandle gOff;
static WinHandle gBg;          /* the last picture, so menu moves skip unpacking it */
static UInt16 gBgPic;
#ifdef DEBUG
static UInt32 gShowTicks;
#endif
static Surface gS;
static Front gFront;
static Boolean gHiRes, gDepthSet, gPalSet;
static UInt32 gOldDepth;
static UInt32 gHeapStart;      /* free dynamic heap at launch */

/* Playing a hole. The hole record stays locked in the course database and
 * the surface grid sits in a record of a scratch database, both out of the
 * dynamic heap. */
#define kScratchName "Par386 Scratch"
#define kCreator     'P386'
#define kCourseType  'Crse'
#define kCoursePre   "Par386 "    /* course databases' names start with this */
/* The saved round: record 0, written by rules_pack. */
#define kSaveName    "Par386 Save"
#define kSaveType    'Save'
#define PIC_SCENE    0xFFFF     /* gBgPic when gBg holds the scene */
static Boolean gPlaying;
static Play gPlay;
static Round gRound;
static Hole gHole;
static SurfaceGrid gGrid;
static DmOpenRef gCourseDb, gScratchDb;
static Course gCourseHdr;      /* the open course's header: scenery and each hole's turn */
/* The installed courses' database names, sorted, found at start-up. */
static char gCourseNames[FRONT_COURSES][dmDBNameLength];
static UInt8 gCourseCount;
static MemHandle gHoleH, gGridH;
static UInt32 gMinFree;        /* least free heap while the scene was drawn */
static Surface gBgS;           /* gBg's pixels, when it holds the scene */
static Boolean gAnim;          /* play wants frames */
static Boolean gRom35;
#ifdef DEBUG
static RenderStats gStats;
static UInt32 gDrawTicks, gMapTicks;
/* ticks and counts of meter and flight frames, and ticks for the last shot's physics */
static UInt32 gMtrTicks, gMtrN, gFlyTicks, gFlyN, gSimTicks;
#endif

#ifdef DEBUG
static char debugLine[48];

/* Calls into the core segment: a sine, and hole 1 of the test course if it's installed. */
static void DebugCoreCheck(void)
{
	LocalID id = DmFindDatabase(0, "Par386 Test");
	Int16 s = isin(ANG_90);
	DmOpenRef db;
	MemHandle h;
	Hole hole;
	int r = -1;

	if (id && (db = DmOpenDatabase(0, id, dmModeReadOnly)) != 0) {
		h = DmQueryRecord(db, 1);
		if (h) {
			r = hole_parse(&hole, MemHandleLock(h), MemHandleSize(h));
			MemHandleUnlock(h);
		}
		DmCloseDatabase(db);
	}
	if (r == HOLE_OK)
		StrPrintF(debugLine, "sin %d, hole %d par %d %d yd", s, hole.number, hole.par, hole.tees[0].yards);
	else
		StrPrintF(debugLine, "sin %d, no test hole (%d)", s, r);
}
#endif

/* Sets the palette from the Pal8 resource (256 RGB triples). */
static void SetPalette(void)
{
	MemHandle h = DmGetResource('Pal8', 1000);
	RGBColorType *pal;
	UInt8 *p;
	UInt16 i;

	if (!h) return;
	if (MemHandleSize(h) < 256 * 3) {
		DmReleaseResource(h);
		return;
	}
	pal = MemPtrNew(256 * sizeof(RGBColorType));
	if (pal) {
		p = MemHandleLock(h);
		for (i = 0; i < 256; i++) {
			pal[i].index = (UInt8)i;
			pal[i].r = p[i * 3];
			pal[i].g = p[i * 3 + 1];
			pal[i].b = p[i * 3 + 2];
		}
		MemHandleUnlock(h);
		gPalSet = WinPalette(winPaletteSet, 0, 256, pal) == errNone;
		MemPtrFree(pal);
	}
	DmReleaseResource(h);
}

/* Picks the mode: 320x320 at 8 bits on double density screens, else
 * 160x160 at 2 bits (1 if that's all there is). Makes the offscreen window
 * and finds its pixels. */
static Err ScreenOpen(void)
{
	UInt32 rom = 0, win = 0, depths = 1, depth, density = kDensityLow;
	Coord w, h;
	UInt16 rb, err = 0;

	FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
	if (rom >= kRom35)
		FtrGet(sysFtrCreator, sysFtrNumWinVersion, &win);
	if (win >= 4)
		WinScreenGetAttribute(winScreenDensity, &density);
	if (WinScreenMode(winScreenModeGetSupportedDepths, NULL, NULL, &depths, NULL) != errNone)
		depths = 1;
	gHiRes = density == kDensityDouble && (depths & 0x80);
	depth = gHiRes ? 8 : (depths & 2) ? 2 : 1;
	if (WinScreenMode(winScreenModeGet, NULL, NULL, &gOldDepth, NULL) == errNone &&
	    WinScreenMode(winScreenModeSet, NULL, NULL, &depth, NULL) == errNone)
		gDepthSet = true;
	else
		return 1;
	if (depth == 8)
		SetPalette();

	gOff = WinCreateOffscreenWindow(160, 160, gHiRes ? nativeFormat : screenFormat, &err);
	if (!gOff) return err ? err : 1;
	MemSet(&gS, sizeof(gS), 0);
	if (rom >= kRom35) {
		BitmapType *b = WinGetBitmap(gOff);
		BmpGetDimensions(b, &w, &h, &rb);
		gS.bits = BmpGetBits(b);
	} else {
		WindowType *wp = (WindowType *)gOff;
		gS.bits = wp->displayAddrV20;
		w = wp->displayWidthV20;
		h = wp->displayHeightV20;
		rb = (UInt16)((((UInt16)w * depth + 15) >> 4) << 1);
	}
	gS.w = w;
	gS.h = h;
	gS.rowBytes = rb;
	gS.depth = (UInt8)depth;
	gS.scale = gHiRes ? 2 : 1;
	if (!gS.bits || w != 160 * gS.scale || h != 160 * gS.scale) return 2;
	gfx_init(&gS);
	gRom35 = rom >= kRom35;
	return errNone;
}

static void ScreenClose(void)
{
	if (gOff) WinDeleteWindow(gOff, false);
	if (gBg) WinDeleteWindow(gBg, false);
	gOff = gBg = NULL;
	gBgPic = 0;
	if (gPalSet) WinPalette(winPaletteSetToDefault, 0, 256, NULL);
	gPalSet = false;
	if (gDepthSet) WinScreenMode(winScreenModeSet, NULL, NULL, &gOldDepth, NULL);
	gDepthSet = false;
}

static void Show(void)
{
	RectangleType r = { { 0, 0 }, { 160, 160 } };

#ifdef DEBUG
	UInt32 t = TimGetTicks();
#endif
	WinCopyRectangle(gOff, WinGetDisplayWindow(), &r, 0, 0, winPaint);
#ifdef DEBUG
	gShowTicks = TimGetTicks() - t;
#endif
}

/* Draws the screen's picture band by band, then the game's text and boxes. */
static void Redraw(void)
{
	UInt32 type = gHiRes ? 'Pic8' : 'Pic2';
	UInt16 id = front_picture(&gFront), k;
	MemHandle h;
	RectangleType r = { { 0, 0 }, { 160, 160 } };
#ifdef DEBUG
	UInt32 t0 = TimGetTicks(), t1, t2;
#endif

	if (gBg && gBgPic == id) {
		WinCopyRectangle(gBg, gOff, &r, 0, 0, winPaint);
	} else {
		k = 0;
		if (gS.depth != 1)
			for (; (h = DmGetResource(type, id + k)) != NULL; k++) {
				gfx_pic_band(&gS, MemHandleLock(h), MemHandleSize(h));
				MemHandleUnlock(h);
				DmReleaseResource(h);
			}
		if (!k)
			gfx_fill(&gS, 0, 0, gS.w, gS.h, UIC_BLACK);
		/* Keep a copy if there's room; without one every redraw unpacks. */
		if (!gBg) {
			UInt16 err;
			gBg = WinCreateOffscreenWindow(160, 160, gHiRes ? nativeFormat : screenFormat, &err);
		}
		if (gBg) {
			WinCopyRectangle(gOff, gBg, &r, 0, 0, winPaint);
			gBgPic = id;
		}
	}
#ifdef DEBUG
	t1 = TimGetTicks();
#endif
	front_draw(&gFront, &gS);
#ifdef DEBUG
	t2 = TimGetTicks();
	if (gFront.screen == SCR_TITLE)
		gfx_text(&gS, 0, 0, debugLine, StrLen(debugLine), UIC_WHITE);
	{
		/* Ticks for the background, the front's drawing and the last copy to
		 * the screen, and the free heap in KB, bottom right. */
		char t[28];
		UInt32 fr = 0, mx = 0;
		MemHeapFreeBytes(0, &fr, &mx);
		StrPrintF(t, "%lu %lu %lu %luK", t1 - t0, t2 - t1, gShowTicks, fr >> 10);
		gfx_text(&gS, gS.w - StrLen(t) * 8 * gS.scale, gS.h - 8 * gS.scale, t, StrLen(t), UIC_WHITE);
	}
#endif
	Show();
}

/* Our keys, or KEY_NONE for ones the system should have (app buttons, Home, power). */
static UInt8 MapKey(const EventType *e)
{
	WChar c = e->data.keyDown.chr;
	UInt16 k = e->data.keyDown.keyCode;

	if (c == vchrNavChange) {
		/* Presses only. Up and down also come as vchrPageUp/Down, so they're left to those. */
		k &= (UInt16)(k >> 8);
		if (k & kNavSelect) return KEY_SELECT;
		if (k & kNavLeft) return KEY_LEFT;
		if (k & kNavRight) return KEY_RIGHT;
		return KEY_NONE;
	}
	switch (c) {
	case vchrPageUp: case vchrRockerUp: case chrUpArrow: return KEY_UP;
	case vchrPageDown: case vchrRockerDown: case chrDownArrow: return KEY_DOWN;
	case vchrRockerLeft: case chrLeftArrow: return KEY_LEFT;
	case vchrRockerRight: case chrRightArrow: return KEY_RIGHT;
	case vchrRockerCenter: case vchrTxCenter: case chrLineFeed: case chrCarriageReturn: case chrSpace:
		return KEY_SELECT;
	}
	if (e->data.keyDown.modifiers & commandKeyMask) return KEY_NONE;
	if (c == 'c' || c == 'C' || c == 's' || c == 'S') return KEY_CARD;
	return c >= 32 && c < 256 ? KEY_OTHER : KEY_NONE;
}

static void Quit(void)
{
	EventType e;

	MemSet(&e, sizeof(e), 0);
	e.eType = appStopEvent;
	EvtAddEventToQueue(&e);
}

/* Redraws two menu lines from the kept picture and copies just those to the screen. */
static Boolean RedrawLines(void)
{
	UInt8 k, line[2];
	URect u;
	RectangleType r;

#ifdef DEBUG
	UInt32 t0 = TimGetTicks();
	char t[12];
#endif

	if (!gBg || gBgPic != front_picture(&gFront)) return false;
	line[0] = gFront.oldSel;
	line[1] = gFront.menu.sel;
	for (k = 0; k < 2; k++) {
		ui_menu_line_rect(&gFront.menu, line[k], &u);
		RctSetRectangle(&r, u.x, u.y, u.w, u.h + 1);
		WinCopyRectangle(gBg, gOff, &r, r.topLeft.x, r.topLeft.y, winPaint);
		ui_menu_draw_line(&gS, &gFront.menu, line[k]);
		WinCopyRectangle(gOff, WinGetDisplayWindow(), &r, r.topLeft.x, r.topLeft.y, winPaint);
	}
#ifdef DEBUG
	/* Ticks for the two lines, bottom left. */
	StrPrintF(t, "L%lu", TimGetTicks() - t0);
	gfx_fill(&gS, 0, gS.h - 8 * gS.scale, 32 * gS.scale, 8 * gS.scale, UIC_BLACK);
	gfx_text(&gS, 0, gS.h - 8 * gS.scale, t, StrLen(t), UIC_WHITE);
	RctSetRectangle(&r, 0, 152, 32, 8);
	WinCopyRectangle(gOff, WinGetDisplayWindow(), &r, 0, 152, winPaint);
#endif
	return true;
}

#ifdef DEBUG
static UInt32 Ticks(void)
{
	return TimGetTicks();
}
#endif

static UInt32 FreeHeap(void)
{
	UInt32 fr = 0, mx = 0;
	MemHeapFreeBytes(0, &fr, &mx);
	return fr;
}

/* surface_raster's row writer: rows go straight into the locked grid record. */
static void GridRow(void *ctx, uint16_t row, const uint8_t *bytes, uint16_t len)
{
	DmWrite(ctx, (UInt32)row * gGrid.stride, bytes, len);
}

static void PlayEnd(void)
{
	LocalID id;

	if (gGridH) MemHandleUnlock(gGridH);
	if (gHoleH) MemHandleUnlock(gHoleH);
	gGridH = gHoleH = NULL;
	if (gScratchDb) DmCloseDatabase(gScratchDb);
	if (gCourseDb) DmCloseDatabase(gCourseDb);
	gScratchDb = gCourseDb = 0;
	if ((id = DmFindDatabase(0, kScratchName)) != 0) DmDeleteDatabase(0, id);
	gPlaying = false;
}

static void DeleteSave(void)
{
	LocalID id = DmFindDatabase(0, kSaveName);
	if (id) DmDeleteDatabase(0, id);
}

/* Saves the round, as it will be after any shot in the air lands, or
 * deletes the save once the round is over. */
static void SaveRound(void)
{
	LocalID id;
	DmOpenRef db;
	MemHandle h;
	UInt8 *buf;
	UInt16 len, at = 0;
	Boolean ok = false;

	if (!gPlaying) return;
	play_settle(&gPlay);
	if (gRound.over) {
		DeleteSave();
		return;
	}
	if ((buf = MemPtrNew(RULES_SAVE_MAX)) == NULL) return;
	len = rules_pack(&gRound, buf);
	if ((id = DmFindDatabase(0, kSaveName)) == 0) {
		if (DmCreateDatabase(0, kSaveName, kCreator, kSaveType, false) == errNone) {
			id = DmFindDatabase(0, kSaveName);
			if (id) {
				UInt16 attr;
				DmDatabaseInfo(0, id, NULL, &attr, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
				attr |= dmHdrAttrBackup;
				DmSetDatabaseInfo(0, id, NULL, &attr, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
			}
		}
	}
	if (id && (db = DmOpenDatabase(0, id, dmModeReadWrite)) != 0) {
		if (DmNumRecords(db) == 0) h = DmNewRecord(db, &at, len);
		else h = DmResizeRecord(db, 0, len) ? DmGetRecord(db, 0) : NULL;
		if (h) {
			ok = DmWrite(MemHandleLock(h), 0, buf, len) == errNone;
			MemHandleUnlock(h);
			DmReleaseRecord(db, 0, true);
		}
		DmCloseDatabase(db);
	}
	/* a save that couldn't be written is removed, not left to resume an older state */
	if (!ok) DeleteSave();
	MemPtrFree(buf);
}

/* Reads the course header from record 0 of an open course database. */
static Boolean CourseHeader(DmOpenRef db, Course *c)
{
	MemHandle h = DmQueryRecord(db, 0);
	Boolean ok;

	if (!h) return false;
	ok = course_parse(c, MemHandleLock(h), MemHandleSize(h)) == HOLE_OK && DmNumRecords(db) > c->hole_count;
	MemHandleUnlock(h);
	return ok;
}

/* Reads the saved round into r. False if there's none, it's of another
 * version or broken, or its course is gone or has changed. */
static Boolean LoadSave(Round *r)
{
	LocalID id = DmFindDatabase(0, kSaveName);
	DmOpenRef db;
	MemHandle h;
	Course c;
	Boolean ok = false;
	UInt8 k;

	if (!id || (db = DmOpenDatabase(0, id, dmModeReadOnly)) == 0) return false;
	if ((h = DmQueryRecord(db, 0)) != NULL) {
		ok = rules_unpack(r, MemHandleLock(h), MemHandleSize(h)) == 0 && !r->over;
		MemHandleUnlock(h);
	}
	DmCloseDatabase(db);
	if (!ok) return false;
	if ((id = DmFindDatabase(0, r->course)) == 0 || (db = DmOpenDatabase(0, id, dmModeReadOnly)) == 0) return false;
	ok = CourseHeader(db, &c) && c.hole_count == r->holes;
	for (k = 0; ok && k < r->holes; k++)
		if (c.par[k] != r->pars[k]) ok = false;
	DmCloseDatabase(db);
	return ok;
}

/* Hole n (from 1) of the open course: parses it, keeping its record
 * locked, and draws its surface grid into record 0 of the scratch database
 * in place of the last hole's. */
static Boolean LoadHole(UInt16 n)
{
	MemHandle h;
	UInt16 at = 0;
	UInt8 row[SURF_ROW_MAX];

	if (gGridH) {
		MemHandleUnlock(gGridH);
		DmRemoveRecord(gScratchDb, 0);
		gGridH = NULL;
	}
	if (gHoleH) MemHandleUnlock(gHoleH);
	gHoleH = NULL;
	if ((h = DmQueryRecord(gCourseDb, n)) == NULL) return false;
	gHoleH = h;
	if (hole_parse(&gHole, MemHandleLock(h), MemHandleSize(h)) != HOLE_OK) return false;
	if (n >= 1 && n <= COURSE_MAX_HOLES) gHole.turn = (UInt16)gCourseHdr.turn[n - 1] << 8;
	surface_size(&gHole, &gGrid);
	if ((h = DmNewRecord(gScratchDb, &at, (UInt32)gGrid.stride * gGrid.h)) == NULL) return false;
	surface_raster(&gHole, row, GridRow, MemHandleLock(h));
	MemHandleUnlock(h);
	DmReleaseRecord(gScratchDb, at, true);
	gGridH = h;
	gGrid.cells = MemHandleLock(h);
	return true;
}

/* Opens the course and the scratch database and starts the round's hole:
 * the saved round if resume, else a new one on the test course for the
 * players picked in the clubhouse, saved straight away. */
/* Set by PlayStart(true) when the saved round itself can't be used. */
static Boolean gSaveBad;

static Boolean PlayStart(Boolean resume)
{
	LocalID id;
	Course c;
	UInt32 seed;

	if (resume) {
		gSaveBad = !LoadSave(&gRound);
		if (gSaveBad) return false;
	} else {
		if (gFront.course >= gCourseCount) return false;
		if ((id = DmFindDatabase(0, gCourseNames[gFront.course])) == 0 ||
		    (gCourseDb = DmOpenDatabase(0, id, dmModeReadOnly)) == 0)
			return false;
		if (!CourseHeader(gCourseDb, &c)) return false;
		DmCloseDatabase(gCourseDb);
		gCourseDb = 0;
#ifdef PLAY_SEED
		/* a fixed wind, for Frond replays that play the holes through */
		seed = PLAY_SEED;
#else
		seed = TimGetSeconds();
#endif
		rules_init(&gRound, gCourseNames[gFront.course], gFront.players, c.hole_count, c.par, seed);
	}
	if ((id = DmFindDatabase(0, gRound.course)) == 0 || (gCourseDb = DmOpenDatabase(0, id, dmModeReadOnly)) == 0)
		return false;
	if (!CourseHeader(gCourseDb, &gCourseHdr)) return false;
	if ((id = DmFindDatabase(0, kScratchName)) != 0) DmDeleteDatabase(0, id);
	if (DmCreateDatabase(0, kScratchName, kCreator, 'Tmp ', false) != errNone) return false;
	if ((id = DmFindDatabase(0, kScratchName)) == 0) return false;
	if ((gScratchDb = DmOpenDatabase(0, id, dmModeReadWrite)) == 0) return false;
	if (!LoadHole(gRound.hole + 1)) return false;
	play_start(&gPlay, &gHole, &gGrid, &gRound);
	gPlaying = true;
	if (!resume) SaveRound();
	return true;
}

/* Locks a resource for render_scene and keeps its handle in hs to let go of after. */
static void LockArt(RenderData *d, UInt32 type, UInt16 id, MemHandle *hs, UInt16 *n)
{
	MemHandle h = DmGetResource(type, id);
	if (!h) return;
	d->p = MemHandleLock(h);
	d->len = MemHandleSize(h);
	hs[(*n)++] = h;
}

/* Copies the scene to gBg, so overlays can be taken off and the map put back
 * without drawing it again, and points gBgS at its pixels. */
static void KeepScene(void)
{
	RectangleType r = { { 0, 0 }, { 160, 160 } };
	UInt16 err;

	if (!gBg) gBg = WinCreateOffscreenWindow(160, 160, gHiRes ? nativeFormat : screenFormat, &err);
	if (gBg) {
		WinCopyRectangle(gOff, gBg, &r, 0, 0, winPaint);
		gBgPic = PIC_SCENE;
		gBgS = gS;
		gBgS.bits = gRom35 ? BmpGetBits(WinGetBitmap(gBg)) : ((WindowType *)gBg)->displayAddrV20;
	}
}

/* Time for the play code, in ms. */
static UInt32 NowMs(void)
{
	UInt32 t = TimGetTicks(), tps = SysTicksPerSecond();
	return (t / tps) * 1000 + (t % tps) * 1000 / tps;
}

/* Copies a rectangle of gOff's pixels to the screen, in standard coordinates. */
static void ShowRect(const URect *d)
{
	Int16 sc = gS.scale, x0 = d->x, y0 = d->y, x1 = d->x + d->w, y1 = d->y + d->h;
	RectangleType r;

	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	x0 /= sc;
	y0 /= sc;
	x1 = (x1 + sc - 1) / sc;
	y1 = (y1 + sc - 1) / sc;
	if (x1 > 160) x1 = 160;
	if (y1 > 160) y1 = 160;
	if (x1 <= x0 || y1 <= y0) return;
	RctSetRectangle(&r, x0, y0, x1 - x0, y1 - y0);
	WinCopyRectangle(gOff, WinGetDisplayWindow(), &r, x0, y0, winPaint);
}

static void SceneRender(void);

/* Shows the strip saying whose turn it is while the scene draws. */
static void TurnStrip(void)
{
	URect u;

	play_turn(&gS, &gPlay);
	u.x = 0;
	u.y = (160 - RENDER_STATUS) * gS.scale;
	u.w = gS.w;
	u.h = RENDER_STATUS * gS.scale;
	ShowRect(&u);
}

#ifdef DEBUG
/* Notes the physics time when a click (or the meter running out) started the flight or putt. */
static void SimTime(UInt8 before, UInt32 t0)
{
	if ((before == PS_DOWN || before == PS_UP) && gPlay.state == PS_FLY) gSimTicks = TimGetTicks() - t0;
}
#endif

/* Lets play move the meter or the ball on and draw over the scene, and copies what changed to the screen. */
static void PlayFrame(void)
{
	int f;
#ifdef DEBUG
	UInt8 st = gPlay.state;
	UInt32 t0 = TimGetTicks();
#endif

	gAnim = false;
	if (!gPlaying || gPlay.view != PLAY_SCENE) return;
	f = play_frame(&gPlay, &gS, gBg && gBgPic == PIC_SCENE ? &gBgS : NULL, NowMs());
	if (f & PL_RENDER) {
		/* the next shot, or aimed off the side: SceneRender comes back here once it's drawn */
		if (f & PL_TURN) TurnStrip();
		SceneRender();
		return;
	}
	if (f & PL_DIRTY) ShowRect(&gPlay.dirty);
	gAnim = (f & PL_ANIM) != 0;
#ifdef DEBUG
	SimTime(st, t0);
	/* frames that drew something, by what they drew */
	if (!(f & PL_DIRTY)) {
	} else if ((st == PS_UP || st == PS_DOWN) && (gPlay.state == PS_UP || gPlay.state == PS_DOWN)) {
		gMtrTicks += TimGetTicks() - t0;
		gMtrN++;
	} else if (st == PS_FLY && gPlay.state == PS_FLY) {
		gFlyTicks += TimGetTicks() - t0;
		gFlyN++;
	}
#endif
}

/* The map's strip. Debug builds put timings on its second line: the last
 * scene draw and the map in ms, the average meter and flight frame in
 * 0.1 ms and the last shot's physics in ms; then the least free heap while
 * drawing and what the game uses of it, after the key is pressed again. */
static void PlayStatus(void)
{
#ifdef DEBUG
	char t[32];
	UInt32 tps = SysTicksPerSecond();
	play_status(&gS, &gPlay, NULL);
	StrPrintF(t, "m%lu %luK u%luK", gMapTicks * 1000 / tps, gMinFree >> 10,
	          gHeapStart > gMinFree ? (gHeapStart - gMinFree) >> 10 : 0);
	gfx_fill(&gS, 0, 0, (Int16)(StrLen(t) * 8 * gS.scale), 8 * gS.scale, UIC_BLACK);
	gfx_text(&gS, 0, 0, t, StrLen(t), UIC_WHITE);
	StrPrintF(t, "S%lu M%lu F%lu P%lu", gDrawTicks * 1000 / tps,
	          gMtrN ? gMtrTicks * 10000 / tps / gMtrN : 0,
	          gFlyN ? gFlyTicks * 10000 / tps / gFlyN : 0, gSimTicks * 1000 / tps);
	gfx_fill(&gS, 0, 8 * gS.scale, (Int16)(StrLen(t) * 8 * gS.scale), 8 * gS.scale, UIC_BLACK);
	gfx_text(&gS, 0, 8 * gS.scale, t, StrLen(t), UIC_WHITE);
#else
	play_status(&gS, &gPlay, NULL);
#endif
}

/* Draws the scene with the art resources locked and the work memory
 * borrowed for the draw, or the green view, which needs neither. */
static void SceneRender(void)
{
	RenderArt art;
	MemHandle hs[RENDER_TYPES * RENDER_SIZES + 3];
	UInt16 n = 0, t, k;
	void *work;
	UInt32 fr;
#ifdef DEBUG
	UInt32 t0;
#endif

	if (gPlay.rv.green) {
#ifdef DEBUG
		t0 = TimGetTicks();
#endif
		render_green(&gS, &gPlay.rv);
#ifdef DEBUG
		gDrawTicks = TimGetTicks() - t0;
		MemSet(&gStats, sizeof(gStats), 0);
#endif
		KeepScene();
		EvtFlushPenQueue();
		EvtFlushKeyQueue();
		PlayFrame();
		return;
	}
	MemSet(&art, sizeof(art), 0);
	for (t = 0; t < RENDER_TYPES; t++)
		for (k = 0; k < RENDER_SIZES; k++)
			LockArt(&art.spr[t][k], 'Spr8', 1000 + t * 10 + k, hs, &n);
	LockArt(&art.golfer, 'Spr8', 2000 + gS.scale - 1, hs, &n);
	/* the course's backdrop, or the first one if it's not in this build */
	LockArt(&art.backdrop, 'Bkd8', 1000 + 10 * gCourseHdr.scenery + gS.scale - 1, hs, &n);
	if (!art.backdrop.p) LockArt(&art.backdrop, 'Bkd8', 1000 + gS.scale - 1, hs, &n);
	work = MemPtrNew(render_work_size(&gS));
	fr = FreeHeap();
	if (fr < gMinFree || !gMinFree) gMinFree = fr;
	if (work) {
#ifdef DEBUG
		render_clock(Ticks);
		t0 = TimGetTicks();
		render_scene(&gS, &gPlay.rv, &art, work, &gStats);
		gDrawTicks = TimGetTicks() - t0;
#else
		render_scene(&gS, &gPlay.rv, &art, work, NULL);
#endif
		MemPtrFree(work);
	} else {
		gfx_fill(&gS, 0, 0, gS.w, gS.h, UIC_BLACK);
	}
	for (k = 0; k < n; k++) {
		MemHandleUnlock(hs[k]);
		DmReleaseResource(hs[k]);
	}
	KeepScene();
	/* taps and keys made while it drew are dropped */
	EvtFlushPenQueue();
	EvtFlushKeyQueue();
	PlayFrame();
}

static void PlayAct(int f)
{
	RectangleType r = { { 0, 0 }, { 160, 160 } };

	if (f & PL_BACK) {
		/* the round is kept to continue, unless it's over */
		gAnim = false;
		SaveRound();
		PlayEnd();
		front_clubhouse(&gFront, !gRound.over);
		Redraw();
	} else if (f & PL_NEXT) {
		gAnim = false;
		if (!LoadHole(gRound.hole + 1)) {
			PlayEnd();
			front_clubhouse(&gFront, 0);
			front_message(&gFront, "No hole", "The course can't\nbe read.");
			Redraw();
			return;
		}
		play_start(&gPlay, &gHole, &gGrid, &gRound);
		TurnStrip();
		SceneRender();
	} else if (f & PL_CARD) {
		/* over the kept scene; saved when it opens at the end of a hole */
		gAnim = false;
		if (gBg && gBgPic == PIC_SCENE) WinCopyRectangle(gBg, gOff, &r, 0, 0, winPaint);
		play_card(&gS, &gPlay);
		Show();
		if (gPlay.card != CARD_VIEW) SaveRound();
	} else if (f & PL_MAP) {
#ifdef DEBUG
		UInt32 t0 = TimGetTicks();
#endif
		gAnim = false;
		play_map(&gS, &gPlay);
#ifdef DEBUG
		gMapTicks = TimGetTicks() - t0;
#endif
		PlayStatus();
		Show();
	} else if ((f & PL_SCENE) && gBg && gBgPic == PIC_SCENE) {
		WinCopyRectangle(gBg, gOff, &r, 0, 0, winPaint);
		PlayFrame();
	} else if (f & (PL_SCENE | PL_RENDER)) {
		if (f & PL_TURN) TurnStrip();
		SceneRender();
	} else {
		PlayFrame();
	}
}

/* A key or tap while playing, with the physics timed in debug builds. */
static void PlayInput(Boolean tap, Int16 x, Int16 y, UInt8 key)
{
#ifdef DEBUG
	UInt8 st = gPlay.state;
	UInt32 t0 = TimGetTicks();
#endif
	int f = tap ? play_tap(&gPlay, x, y, NowMs()) : play_key(&gPlay, key, NowMs());
#ifdef DEBUG
	SimTime(st, t0);
	if (st == PS_AIM && gPlay.state == PS_UP) gMtrTicks = gMtrN = gFlyTicks = gFlyN = 0;
#endif
	PlayAct(f);
}

static void Act(int r)
{
	if (r & (FR_PLAY | FR_CONTINUE)) {
		if (PlayStart((r & FR_CONTINUE) != 0)) {
			TurnStrip();
			SceneRender();
		} else if ((r & FR_CONTINUE) && gSaveBad) {
			PlayEnd();
			DeleteSave();
			front_clubhouse(&gFront, 0);
			front_message(&gFront, "Can't continue", "The saved round's\ncourse is missing.");
			Redraw();
		} else if (r & FR_CONTINUE) {
			/* the save is fine, there wasn't the memory to open the hole */
			PlayEnd();
			front_clubhouse(&gFront, 1);
			front_message(&gFront, "Out of memory", "Free some storage\nand try again.");
			Redraw();
		} else {
			PlayEnd();
			front_clubhouse(&gFront, LoadSave(&gRound));
			front_message(&gFront, "No course", "Install a course\nPDB and try again.");
			Redraw();
		}
		return;
	}
	if (r & FR_QUIT) Quit();
	else if ((r & FR_LINES) && RedrawLines()) return;
	else if (r & FR_REDRAW) Redraw();
}

#ifdef DEBUG
/* n, or a tap in the top left corner, gives everyone par for the rest of
 * the hole and goes on to the next, for walking through a course's tee
 * views in Frond. */
static void SkipHole(void)
{
	UInt8 i;

	if (rules_hole_over(&gRound)) return;
	for (i = 0; i < gRound.players; i++)
		if (!gRound.pl[i].holed) rules_hole_out(&gRound, i, gRound.pars[gRound.hole]);
	rules_advance(&gRound);
	PlayAct(gRound.over ? PL_BACK : PL_NEXT);
}
#endif

static Boolean MainFormHandleEvent(EventType *e)
{
	switch (e->eType) {
	case frmOpenEvent:
		FrmDrawForm(FrmGetActiveForm());
		Redraw();
		return true;
	case frmUpdateEvent:
		Show();
		return true;
	case penDownEvent:
#ifdef DEBUG
		/* a tap in the top left corner skips the hole, as n does */
		if (gPlaying && e->screenX < 8 && e->screenY < 8) {
			SkipHole();
			return true;
		}
#endif
		if (gPlaying) PlayInput(true, e->screenX, e->screenY, 0);
		else Act(front_tap(&gFront, e->screenX, e->screenY));
		return true;
	default:
		return false;
	}
}

static Boolean ApplicationHandleEvent(EventType *e)
{
	if (e->eType == frmLoadEvent) {
		UInt16 id = e->data.frmLoad.formID;
		FormType *form = FrmInitForm(id);
		FrmSetActiveForm(form);
		if (id == MainForm) FrmSetEventHandler(form, MainFormHandleEvent);
		return true;
	}
	return false;
}


static void EventLoop(void)
{
	EventType e;
	UInt16 err;
	UInt8 key;

	do {
		/* a tick between frames while the meter or the ball moves */
		EvtGetEvent(&e, gAnim ? 1 : evtWaitForever);
		if (gAnim) PlayFrame();
		if (e.eType == nilEvent && gPlaying) continue;
		if (e.eType == keyDownEvent && gPlaying) {
			/* the Menu button and backspace go back to the clubhouse */
			WChar c = e.data.keyDown.chr;
#ifdef DEBUG
			if (c == 'n' || c == 'N') {
				SkipHole();
				continue;
			}
#endif
			key = (c == vchrMenu || c == chrBackspace || c == chrEscape) ? KEY_BACK : MapKey(&e);
			/* held keys repeat for aiming and clubs only, or they'd queue up clicks and toggles */
			if ((e.data.keyDown.modifiers & autoRepeatKeyMask) && key != KEY_LEFT && key != KEY_RIGHT &&
			    key != KEY_UP && key != KEY_DOWN) continue;
			if (key != KEY_NONE) {
				PlayInput(false, 0, 0, key);
				continue;
			}
		}
		if (e.eType == keyDownEvent && (key = MapKey(&e)) != KEY_NONE) {
			Act(front_key(&gFront, key));
			continue;
		}
		if (SysHandleEvent(&e)) continue;
		if (MenuHandleEvent(0, &e, &err)) continue;
		if (ApplicationHandleEvent(&e)) continue;
		FrmDispatchEvent(&e);
	} while (e.eType != appStopEvent);
}

/* Finds the installed course databases (type Crse, ours) and gives their
 * names, less "Par386 ", to the clubhouse, sorted, the first picked. */
static void FindCourses(void)
{
	DmSearchStateType st;
	UInt16 card;
	LocalID id;
	Boolean first = true;
	char name[dmDBNameLength];
	const char *shown[FRONT_COURSES];
	UInt8 i, k, pre = StrLen(kCoursePre);

	gCourseCount = 0;
	while (DmGetNextDatabaseByTypeCreator(first, &st, kCourseType, kCreator, false, &card, &id) == errNone) {
		first = false;
		if (card != 0 || DmDatabaseInfo(card, id, name, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) != errNone)
			continue;
		if (StrNCompare(name, kCoursePre, pre) != 0 || !name[pre]) continue;
		/* keep the first FRONT_COURSES names in sorted order */
		i = gCourseCount;
		if (i == FRONT_COURSES) {
			if (StrCompare(gCourseNames[i - 1], name) <= 0) continue;
			i--;
		} else gCourseCount++;
		for (; i > 0 && StrCompare(gCourseNames[i - 1], name) > 0; i--)
			StrCopy(gCourseNames[i], gCourseNames[i - 1]);
		StrCopy(gCourseNames[i], name);
	}
	for (k = 0; k < gCourseCount; k++) shown[k] = gCourseNames[k] + pre;
	front_courses(&gFront, shown, gCourseCount, 0);
}

/* Globals are only safe on a normal launch, so other launch codes do nothing here. */
UInt32 PilotMain(UInt16 cmd, void *cmdPBP, UInt16 launchFlags)
{
	UInt32 rom = 0;

	if (cmd != sysAppLaunchCmdNormalLaunch) return 0;
	FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
	if (rom < kMinRomVersion) {
		FrmAlert(RomIncompatibleAlert);
		return sysErrRomIncompatible;
	}
	gHeapStart = FreeHeap();
	if (ScreenOpen() != errNone) {
		ScreenClose();
		FrmAlert(ScreenAlert);
		return 0;
	}
#ifdef DEBUG
	DebugCoreCheck();
#endif
	front_init(&gFront);
	FindCourses();
	front_clubhouse(&gFront, LoadSave(&gRound));
	gFront.screen = SCR_TITLE;
	FrmGotoForm(MainForm);
	EventLoop();
	FrmCloseAllForms();
	/* leaving mid-round saves it */
	if (gPlaying) {
		SaveRound();
		PlayEnd();
	}
	ScreenClose();
	return 0;
}
