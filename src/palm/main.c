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
#define PIC_SCENE    0xFFFF     /* gBgPic when gBg holds the scene */
static Boolean gPlaying;
static Play gPlay;
static Hole gHole;
static SurfaceGrid gGrid;
static DmOpenRef gCourseDb, gScratchDb;
static MemHandle gHoleH, gGridH;
static UInt32 gMinFree;        /* least free heap while the scene was drawn */
#ifdef DEBUG
static RenderStats gStats;
static UInt32 gDrawTicks, gMapTicks;
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

/* Opens hole 1 of the test course and draws its surface grid into the scratch record. */
static Boolean PlayStart(void)
{
	LocalID id = DmFindDatabase(0, "Par386 Test");
	MemHandle h;
	UInt16 at = 0;
	UInt8 row[SURF_ROW_MAX];

	if (!id || (gCourseDb = DmOpenDatabase(0, id, dmModeReadOnly)) == 0) return false;
	if ((h = DmQueryRecord(gCourseDb, 1)) == NULL) return false;
	gHoleH = h;
	if (hole_parse(&gHole, MemHandleLock(h), MemHandleSize(h)) != HOLE_OK) return false;
	surface_size(&gHole, &gGrid);
	if ((id = DmFindDatabase(0, kScratchName)) != 0) DmDeleteDatabase(0, id);
	if (DmCreateDatabase(0, kScratchName, kCreator, 'Tmp ', false) != errNone) return false;
	if ((id = DmFindDatabase(0, kScratchName)) == 0) return false;
	if ((gScratchDb = DmOpenDatabase(0, id, dmModeReadWrite)) == 0) return false;
	if ((h = DmNewRecord(gScratchDb, &at, (UInt32)gGrid.stride * gGrid.h)) == NULL) return false;
	surface_raster(&gHole, row, GridRow, MemHandleLock(h));
	MemHandleUnlock(h);
	DmReleaseRecord(gScratchDb, at, true);
	gGridH = h;
	gGrid.cells = MemHandleLock(h);
	play_init(&gPlay, &gHole, &gGrid);
	gPlaying = true;
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

static void KeepScene(void)
{
	RectangleType r = { { 0, 0 }, { 160, 160 } };
	UInt16 err;

	if (!gBg) gBg = WinCreateOffscreenWindow(160, 160, gHiRes ? nativeFormat : screenFormat, &err);
	if (gBg) {
		WinCopyRectangle(gOff, gBg, &r, 0, 0, winPaint);
		gBgPic = PIC_SCENE;
	}
}

static void PlayStatus(Boolean map)
{
#ifdef DEBUG
	/* Scene: draw time in ms, free heap at its lowest while drawing and what
	 * the game uses of it. Map: its draw time, then the scene's stages in
	 * ticks (gather, terrain, sky, objects, golfer). */
	char t[32];
	UInt32 tps = SysTicksPerSecond();
	if (map)
		StrPrintF(t, "M%lu %lu/%lu/%lu/%lu/%lu", gMapTicks * 1000 / tps, gStats.ticks[RS_GATHER],
		          gStats.ticks[RS_TERRAIN], gStats.ticks[RS_SKY], gStats.ticks[RS_OBJECTS], gStats.ticks[RS_GOLFER]);
	else
		StrPrintF(t, "%lums %luK u%luK", gDrawTicks * 1000 / tps, gMinFree >> 10,
		          gHeapStart > gMinFree ? (gHeapStart - gMinFree) >> 10 : 0);
	play_status(&gS, &gPlay, t);
#else
	(void)map;
	play_status(&gS, &gPlay, NULL);
#endif
}

/* Draws the scene with the art resources locked and the work memory borrowed for the draw. */
static void SceneRender(void)
{
	RenderArt art;
	MemHandle hs[RENDER_TYPES * RENDER_SIZES + 2];
	UInt16 n = 0, t, k;
	void *work;
	UInt32 fr;
#ifdef DEBUG
	UInt32 t0;
#endif

	MemSet(&art, sizeof(art), 0);
	for (t = 0; t < RENDER_TYPES; t++)
		for (k = 0; k < RENDER_SIZES; k++)
			LockArt(&art.spr[t][k], 'Spr8', 1000 + t * 10 + k, hs, &n);
	LockArt(&art.golfer, 'Spr8', 2000 + gS.scale - 1, hs, &n);
	LockArt(&art.backdrop, 'Bkd8', 1000 + gS.scale - 1, hs, &n);
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
	PlayStatus(false);
	KeepScene();
	Show();
}

static void PlayAct(int f)
{
	RectangleType r = { { 0, 0 }, { 160, 160 } };

	if (f & PL_BACK) {
		PlayEnd();
		gFront.screen = SCR_MENU;
		Redraw();
	} else if (f & PL_MAP) {
#ifdef DEBUG
		UInt32 t0 = TimGetTicks();
#endif
		play_map(&gS, &gPlay);
#ifdef DEBUG
		gMapTicks = TimGetTicks() - t0;
#endif
		PlayStatus(true);
		Show();
	} else if ((f & PL_SCENE) && gBg && gBgPic == PIC_SCENE) {
		WinCopyRectangle(gBg, gOff, &r, 0, 0, winPaint);
		Show();
	} else if (f & (PL_SCENE | PL_RENDER)) {
		SceneRender();
	}
}

static void Act(int r)
{
	if (r & FR_PLAY) {
		if (PlayStart()) {
			SceneRender();
		} else {
			PlayEnd();
			front_message(&gFront, "No course", "Install\nPar386-Test.pdb\nand try again.");
			Redraw();
		}
		return;
	}
	if (r & FR_QUIT) Quit();
	else if ((r & FR_LINES) && RedrawLines()) return;
	else if (r & FR_REDRAW) Redraw();
}

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
		if (gPlaying) PlayAct(play_tap(&gPlay, e->screenX, e->screenY));
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
		EvtGetEvent(&e, evtWaitForever);
		if (e.eType == keyDownEvent && gPlaying) {
			/* the Menu button and backspace go back to the clubhouse */
			WChar c = e.data.keyDown.chr;
			/* a held key would queue up toggles during the draw */
			if (e.data.keyDown.modifiers & autoRepeatKeyMask) continue;
			key = (c == vchrMenu || c == chrBackspace || c == chrEscape) ? KEY_BACK : MapKey(&e);
			if (key != KEY_NONE) {
				PlayAct(play_key(&gPlay, key));
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
	FrmGotoForm(MainForm);
	EventLoop();
	FrmCloseAllForms();
	if (gPlaying) PlayEnd();
	ScreenClose();
	return 0;
}
