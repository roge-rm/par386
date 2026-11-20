/* The screen. Modes, deepest first:
 * - 16-bit colour: the screen stays at 16 bits; the game draws 8-bit
 *   palette indexes into a bitmap and gfx_to16 turns what changed into the
 *   16-bit offscreen window that's copied to the display.
 * - 8-bit colour with the master palette set.
 * - 4, 2 or 1 bit greys, drawn straight into the offscreen window.
 * Double density screens (OS 5) and Sony HR Clies (OS 4, the HR library)
 * draw at 2 pixels a unit. Where the input area can be closed (PINs), the
 * game takes the whole display, up to 240 units across or down, and
 * ScreenResize follows it when it opens, closes or turns. Sony Clies with
 * a silk area that can be collapsed (the Silk library or Virtual Silk) do
 * the same; Garmin iQues use the same PINS calls as Palm. */
#define ALLOW_ACCESS_TO_INTERNALS_OF_WINDOWS
#include <PalmOS.h>
#include "screen.h"
#include "dana.h"
#include "sonyhr.h"
#include "silk.h"
#include "arm.h"

#define kRom35 sysMakeROMVersion(3, 5, 0, sysROMStageRelease, 0)
#define kSonyCreator 'SoNy'

Surface gS;
static Surface gLCD;            /* the whole Dana screen while gS holds the course */
static UInt8 *gDanaDisplay;
static Int16 gDanaOrigin;

static WinHandle gOff;          /* copied to the display */
static WinHandle gBg;           /* the kept copy, when it's a window */
static BitmapType *gS8, *gBg8;  /* 16-bit mode: the 8-bit surface and its kept copy */
static Surface gBgS;
static Boolean gHaveBg;
static UInt8 *gOffBits;         /* 16-bit mode: gOff's pixels */
static UInt16 gOffRb;
static Boolean gLE;             /* gOff's 16-bit pixels are low byte first */
static Boolean g16, gDD, gRom35, gPins, gPinsClosed, gSmall;
static Boolean gDepthSet, gPalSet;
static UInt32 gOldDepth, gDepths, gPinsVer;
static UInt16 gPinsOld;         /* the input area's state at launch */
#ifdef DEBUG
static UInt8 gFailMakes;        /* Make fails this many more times */
#endif
static UInt8 gDepth;            /* the screen's depth */
/* Sony HR */
static Boolean gSony, gSonyLoaded, gSonyOpen, gSonyMode;
static Boolean gOS5;            /* an ARM device: fast enough for the views at 320 */
static UInt16 gSonyRef;
static UInt32 gSonyW, gSonyH, gSonyDepth;
static Boolean gSonyColour;
/* Sony silk */
static Boolean gSilk, gSilkLoaded, gVsk;
static UInt16 gSilkRef, gVskOld;
/* registered for the display's resize notification */
static UInt32 gNotifyType;
static Boolean gResizing;

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

/* Only removes a library loaded here, after it's closed. */
static void SonyClose(void)
{
	Err err = errNone;

	if (gSonyMode)
		HRWinScreenMode(gSonyRef, winScreenModeSet, &gSonyW, &gSonyH, &gSonyDepth, &gSonyColour);
	if (gSonyOpen) err = HRClose(gSonyRef);
	if (gSonyLoaded && err == errNone) SysLibRemove(gSonyRef);
	gSony = gSonyLoaded = gSonyOpen = gSonyMode = false;
}

/* Opens the Sony HR library and keeps the mode to put back; the mode is
 * set with the depth. */
static Boolean SonyOpen(void)
{
	UInt16 version;

	if (SysLibFind("Sony HR Library", &gSonyRef) != errNone) {
		if (SysLibLoad('libr', 'SlHr', &gSonyRef) != errNone) return false;
		gSonyLoaded = true;
	}
	if (HROpen(gSonyRef) != errNone) {
		SonyClose();
		return false;
	}
	gSonyOpen = true;
	if (HRGetAPIVersion(gSonyRef, &version) != errNone || !version ||
	    HRWinScreenMode(gSonyRef, winScreenModeGet, &gSonyW, &gSonyH, &gSonyDepth, &gSonyColour) != errNone) {
		SonyClose();
		return false;
	}
	gSony = true;
	return true;
}

/* Sets the screen's depth, keeping the old one to put back. */
static Boolean SetDepth(UInt32 depth)
{
	if (gSony) {
		UInt32 w = 320, h = 320;
		if (HRWinScreenMode(gSonyRef, winScreenModeSet, &w, &h, &depth, NULL) != errNone) return false;
		gSonyMode = true;
		return true;
	}
	if (!gDepthSet && WinScreenMode(winScreenModeGet, NULL, NULL, &gOldDepth, NULL) != errNone) return false;
	if (WinScreenMode(winScreenModeSet, NULL, NULL, &depth, NULL) != errNone) return false;
	gDepthSet = true;
	return true;
}

/* An offscreen window of the screen's depth, w x h pixels. */
static WinHandle NewWin(Int16 w, Int16 h)
{
	UInt16 err;

	if (gSony) return HRWinCreateOffscreenWindow(gSonyRef, w, h, screenFormat, &err);
	if (gDD) return WinCreateOffscreenWindow(w / 2, h / 2, nativeFormat, &err);
	return WinCreateOffscreenWindow(w, h, screenFormat, &err);
}

/* A window's pixels and bytes a row. */
static UInt8 *WinBits(WinHandle win, UInt16 *rb)
{
	if (gRom35) {
		BitmapType *b = WinGetBitmap(win);
		Coord w, h;
		BmpGetDimensions(b, &w, &h, rb);
		return BmpGetBits(b);
	} else {
		WindowType *wp = (WindowType *)win;
		*rb = (UInt16)((((UInt16)wp->displayWidthV20 * gDepth + 15) >> 4) << 1);
		return wp->displayAddrV20;
	}
}

static void Free(void)
{
	if (gOff) WinDeleteWindow(gOff, false);
	if (gBg) WinDeleteWindow(gBg, false);
	if (gS8) BmpDelete(gS8);
	if (gBg8) BmpDelete(gBg8);
	gOff = gBg = NULL;
	gS8 = gBg8 = NULL;
	gOffBits = NULL;
	gHaveBg = false;
	ScreenFadeDrop();
	ScreenSpareDrop();
	/* nothing draws until Make works again */
	gS.bits = NULL;
	gLCD.bits = NULL;
	gDanaDisplay = NULL;
	gDanaOrigin = 0;
	gS.w = gS.h = gS.lw = gS.lh = 0;
	gS.rowBytes = 0;
}

/* The size to draw at, in pixels: the display, at most 240 units either way
 * and a multiple of 16 pixels across, or 160 units square when gSmall. */
/* The display's size in units. Sony HR gives pixels. */
static void Extent(Coord *ew, Coord *eh)
{
	if (gSony) {
		HRWinGetDisplayExtent(gSonyRef, ew, eh);
		*ew /= 2;
		*eh /= 2;
	} else {
		WinGetDisplayExtent(ew, eh);
	}
}

static void SEC_DANA Size(Int16 *w, Int16 *h)
{
	Coord ew = 160, eh = 160;
	Int16 sc = gS.scale;

	if (gRom35 && (!gSony || gSilk) && !gSmall) Extent(&ew, &eh);
	if (sc == 1 && ew == DANA_SCREEN_W && eh == 160) {
		*w = ew;
		*h = eh;
		return;
	}
	if (ew > 240) ew = 240;
	if (eh > 240) eh = 240;
	if (ew < 160) ew = 160;
	if (eh < 160) eh = 160;
	*w = (Int16)((ew * sc) & ~15);
	*h = (Int16)(eh * sc);
}

/* Whether gOff's 16-bit pixels are low byte first (ARM devices): writes
 * pure red high byte first to the top left pixel and asks the OS what it
 * sees there. */
static Boolean Swapped(void)
{
	RGBColorType c;
	WinHandle old;
	UInt32 win = 0, rom = 0;

	/* WinGetPixelRGB is missing from some OS 3.5 builds */
	FtrGet(sysFtrCreator, sysFtrNumWinVersion, &win);
	FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
	if (win < 4 && rom < 0x05000000UL) return false;
	old = WinSetDrawWindow(gOff);
	gOffBits[0] = 0xF8;
	gOffBits[1] = 0x00;
	WinGetPixelRGB(0, 0, &c);
	WinSetDrawWindow(old);
	return c.r < 0x80;
}

/* Makes the buffers for the current size. */
static Err Make(void)
{
	Int16 w, h;
	UInt16 rb, err;

#ifdef DEBUG
	if (gFailMakes) {
		gFailMakes--;
		return 1;
	}
#endif
	Size(&w, &h);
	gOff = NewWin(w, h);
	if (!gOff) return 1;
	/* an OS 5 window can stay at the hardware's depth after a lower one is set */
	if (gRom35 && !gSony && BmpGetBitDepth(WinGetBitmap(gOff)) != (g16 ? 16 : gDepth)) return 3;
	if (g16) {
		gOffBits = WinBits(gOff, &gOffRb);
		gLE = Swapped();
		gS8 = BmpCreate(w, h, 8, NULL, &err);
		if (!gS8) return 1;
		gS.bits = BmpGetBits(gS8);
		{
			Coord bw, bh;
			BmpGetDimensions(gS8, &bw, &bh, &rb);
		}
	} else {
		gS.bits = WinBits(gOff, &rb);
	}
	gS.w = w;
	gS.h = h;
	gS.rowBytes = rb;
	gS.depth = g16 ? 8 : gDepth;
	if (!gS.bits) return 2;
	gfx_init(&gS);
	if (w == DANA_SCREEN_W && gS.scale == 1 && !g16) {
		WinHandle display = WinGetDisplayWindow();
		BitmapType *bm = WinGetBitmap(display);
		RectangleType bounds;
		Coord dw, dh;
		UInt16 drb;
		WinGetBounds(display, &bounds);
		gDanaOrigin = bounds.topLeft.x;
		if (bm) {
			BmpGetDimensions(bm, &dw, &dh, &drb);
			if (dw == w && dh == h && drb == rb && BmpGetBitDepth(bm) == gS.depth)
				gDanaDisplay = BmpGetBits(bm);
		}
	}
	/* 68k devices (before OS 5) draw the views at 160 and double them at 320 */
	gS.half = gS.scale == 2 && !gOS5;
	return errNone;
}

/* Collapses the Sony silk area, keeping what to put back. */
static void SilkOpen(void)
{
	UInt32 info = 0, ver;
	UInt16 dirs;

	FtrGet(kSonyCreator, sonyFtrSysInfo, &info);
	if (FtrGet(kSonyCreator, sonyFtrVskVersion, &ver) != errNone) ver = 0;
	if (!ver && !(info && (((SonySysInfo *)info)->libr & sonyLibrSilk))) return;
	if (SysLibFind(silkLibName, &gSilkRef) != errNone) {
		if (SysLibLoad(sysFileTLibrary, silkLibCreator, &gSilkRef) != errNone) return;
		gSilkLoaded = true;
	}
	if (ver) {
		if (VskOpen(gSilkRef) == errNone) {
			gSilk = gVsk = true;
			if (VskGetState(gSilkRef, vskStateResize, &gVskOld) != errNone) gVskOld = vskResizeMax;
			/* later versions resize across too (the UX turns) */
			dirs = ver >= vskVersionNum2 ? vskResizeVertically | vskResizeHorizontally : vskResizeVertically;
			if (VskSetState(gSilkRef, vskStateEnable, dirs) != errNone)
				VskSetState(gSilkRef, vskStateEnable, vskResizeVertically);
			VskSetState(gSilkRef, vskStateResize, vskResizeNone);
		}
	} else if (SilkLibOpen(gSilkRef) == errNone) {
		gSilk = true;
		SilkLibEnableResize(gSilkRef);
		SilkLibResizeDispWin(gSilkRef, silkResizeMax);
	}
	if (!gSilk && gSilkLoaded) {
		SysLibRemove(gSilkRef);
		gSilkLoaded = false;
	}
}

/* Puts the silk area back and only removes a library loaded here. */
static void SilkClose(void)
{
	Err err = errNone;

	if (gVsk) {
		VskSetState(gSilkRef, vskStateResize, gVskOld);
		VskSetState(gSilkRef, vskStateEnable, vskResizeDisable);
		err = VskClose(gSilkRef);
	} else if (gSilk) {
		SilkLibResizeDispWin(gSilkRef, silkResizeNormal);
		SilkLibDisableResize(gSilkRef);
		err = SilkLibClose(gSilkRef);
	}
	if (gSilkLoaded && err == errNone) SysLibRemove(gSilkRef);
	gSilk = gSilkLoaded = gVsk = false;
}

/* The display changed size: a winDisplayChangedEvent for the main form,
 * as Palm's PINS sends itself. Ours while ScreenResize runs are left out. */
static Err Notified(SysNotifyParamType *p)
{
	EventType e;

	(void)p;
	if (gResizing) return errNone;
	MemSet(&e, sizeof(e), 0);
	e.eType = winDisplayChangedEvent;
	EvtAddUniqueEventToQueue(&e, 0, true);
	return errNone;
}

/* Garmin's PINS only broadcasts sysNotifyDisplayResizedEvent; Sony's silk
 * the display change. Taken off again in ScreenClose. */
static void NotifyOpen(void)
{
	UInt16 card;
	LocalID db;
	UInt32 type = gSilk ? sysNotifyDisplayChangeEvent : sysNotifyDisplayResizedEvent;

	if (!gSilk && !gPins) return;
	if (SysCurAppDatabase(&card, &db) != errNone) return;
	if (SysNotifyRegister(card, db, type, Notified, sysNotifyNormalPriority, NULL) == errNone)
		gNotifyType = type;
}

static void NotifyClose(void)
{
	UInt16 card;
	LocalID db;

	if (gNotifyType && SysCurAppDatabase(&card, &db) == errNone)
		SysNotifyUnregister(card, db, gNotifyType, sysNotifyNormalPriority);
	gNotifyType = 0;
}

static Int16 StepDown(void);

Err ScreenOpen(void)
{
	UInt32 rom = 0, win = 0, depths = 1, density = kDensityLow, v, sv, depth;
	Int16 res;

	ArmOpen();
	FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
	gRom35 = rom >= kRom35;
	gOS5 = rom >= 0x05000000UL;
	if (gRom35) FtrGet(sysFtrCreator, sysFtrNumWinVersion, &win);
	if (win >= 4) WinScreenGetAttribute(winScreenDensity, &density);
	gDD = density == kDensityDouble;
	gPins = FtrGet(pinCreator, pinFtrAPIVersion, &v) == errNone;
	if (gPins) gPinsVer = v;
	if (FtrGet(kSonyCreator, sonyFtrSysInfo, &sv) == errNone) {
		if (!gDD && win < 4) SonyOpen();
		if (gRom35) SilkOpen();
	}
	if (WinScreenMode(winScreenModeGetSupportedDepths, NULL, NULL, &depths, NULL) != errNone)
		depths = 1;
	gDepths = depths;

	/* the deepest the screen has; colour needs OS 3.5 */
	if (gRom35 && (depths & 0x8000)) depth = 16;
	else if (gRom35 && (depths & 0x80)) depth = 8;
	else if (depths & 0x08) depth = 4;
	else if (depths & 0x02) depth = 2;
	else depth = 1;
#if defined(DEBUG) && defined(FORCE_DEPTH)
	depth = FORCE_DEPTH;
#endif
	if (!SetDepth(depth)) {
		/* a 16-bit screen that won't say so, or a depth it refuses */
		if (depth == 16 && gRom35 && (depths & 0x80) && SetDepth(8)) depth = 8;
		else if (depth > 2 && (depths & 0x02) && SetDepth(2)) depth = 2;
		else return 1;
	}
	gDepth = (UInt8)depth;
	g16 = depth == 16;
	if (depth == 8) SetPalette();
	MemSet(&gS, sizeof(gS), 0);
	gS.scale = gDD || gSony ? 2 : 1;
	if (Make() != errNone && g16) {
		/* no room for the 16-bit buffers; depth changes here must not notify */
		gResizing = true;
		res = StepDown();
		gResizing = false;
		if (res == SCREEN_FAILED) return 1;
	}
	if (!gOff || !gS.bits) return 1;
	if (gRom35) NotifyOpen();
	return errNone;
}

void ScreenClose(void)
{
	NotifyClose();
	Free();
	ArmClose();
	if (gPalSet) WinPalette(winPaletteSetToDefault, 0, 256, NULL);
	gPalSet = false;
	if (gDepthSet) WinScreenMode(winScreenModeSet, NULL, NULL, &gOldDepth, NULL);
	gDepthSet = false;
	SonyClose();
	SilkClose();
	/* pinInputAreaUser is PINS 1.1 (OS 5.3) */
	if (gPinsClosed) PINSetInputAreaState(gPinsVer >= pinAPIVersion1_1 ? pinInputAreaUser : gPinsOld);
	gPinsClosed = false;
}

void ScreenFormInit(FormType *frm)
{
	if (!gPins) return;
	/* up to 240 units either way, so the input area can close and the screen turn */
	WinSetConstraintsSize(FrmGetWindowHandle(frm), 160, 240, 240, 160, 240, 240);
	FrmSetDIAPolicyAttr(frm, frmDIAPolicyCustom);
	PINSetInputTriggerState(pinInputTriggerEnabled);
	if (!gPinsClosed) {
		gPinsOld = PINGetInputAreaState();
		if (gPinsOld == pinInputAreaNone) gPinsOld = pinInputAreaOpen;
	}
	PINSetInputAreaState(pinInputAreaClosed);
	gPinsClosed = true;
}

/* Sets a lower depth while running: the palette goes back to the
 * system's when leaving 8 bits. */
static Boolean StepDepth(UInt32 depth)
{
	if (!SetDepth(depth)) return false;
	if (depth != 8 && gPalSet) {
		WinPalette(winPaletteSetToDefault, 0, 256, NULL);
		gPalSet = false;
	}
	gDepth = (UInt8)depth;
	g16 = false;
	if (depth == 8) SetPalette();
	return true;
}

/* No room for the buffers: steps down until they fit. */
static Int16 StepDown(void)
{
	WinHandle old;
	UInt8 have, k;

	/* no room: 8 bits with the palette, as ScreenOpen does */
	Free();
	if (g16 && StepDepth(8) && Make() == errNone) return SCREEN_NEW;
	/* then 160 units square at 2 bits, 1, or the depth it had, top left on
	 * a blank display */
	have = gDepth > 8 ? 8 : gDepth;
	gSmall = true;
	for (k = 0; k < 3; k++) {
		Free();
		if (k == 0 && !(gDepths & 0x02)) continue;
		if (StepDepth(k == 0 ? 2 : k == 1 ? 1 : have) && Make() == errNone) {
			old = WinSetDrawWindow(WinGetDisplayWindow());
			WinEraseWindow();
			WinSetDrawWindow(old);
			return SCREEN_NEW;
		}
	}
	Free();
	return SCREEN_FAILED;
}

/* Makes the buffers again for a new size, stepping down if there's no room. */
static Int16 Remake(void)
{
	Free();
	if (Make() == errNone) return SCREEN_NEW;
	return StepDown();
}

Int16 ScreenResize(void)
{
	Int16 w, h, res;
	Boolean playing = gLCD.bits != NULL;
	RectangleType r;
	FormType *frm = FrmGetActiveForm();

	if (!gRom35 || (gSony && !gSilk)) return SCREEN_SAME;
	/* the form's window takes the whole display */
	if (frm) {
		Coord ew, eh;
		Extent(&ew, &eh);
		RctSetRectangle(&r, 0, 0, ew, eh);
		WinSetBounds(FrmGetWindowHandle(frm), &r);
	}
	gSmall = false;
	Size(&w, &h);
	if (gS.bits && w == (playing ? gLCD.w : gS.w) && h == gS.h) return SCREEN_SAME;
	/* depth changes made here notify too */
	gResizing = true;
	res = Remake();
	gResizing = false;
	if (playing && res == SCREEN_NEW) ScreenGame(true);
	return res;
}

void ScreenGame(Boolean playing)
{
	if (!playing && gLCD.bits) {
		ScreenDropKept();
		gS = gLCD;
		gLCD.bits = NULL;
	} else if (playing && gS.w == DANA_SCREEN_W && gS.h == 160 && !g16) {
		ScreenDropKept();
		gLCD = gS;
		gS.bits += (DANA_RAIL_W << gS.lg) >> 3;
		gS.w = gS.lw = DANA_SCENE_W;
		gS.fullview = 1;
	}
}

Boolean ScreenDanaPane(Boolean right, Surface *s)
{
	if (!gLCD.bits) return false;
	*s = gLCD;
	if (right) s->bits += ((DANA_SCREEN_W - DANA_RAIL_W) << s->lg) >> 3;
	s->w = s->lw = DANA_RAIL_W;
	return true;
}

/* The Dana keeps a 160-wide coordinate origin in some drawing calls. */
static void SEC_DANA DanaBlit(Int16 x, Int16 y, Int16 w, Int16 h)
{
	RectangleType r;
	const Surface *lcd = gLCD.bits ? &gLCD : &gS;

	if (gDanaDisplay) {
		Int16 first = (x << lcd->lg) >> 3;
		Int16 n = (((x + w) << lcd->lg) + 7) / 8 - first;
		UInt32 at = (UInt32)y * lcd->rowBytes + first;
		for (; h > 0; h--, at += lcd->rowBytes)
			MemMove(gDanaDisplay + at, lcd->bits + at, n);
	} else {
		RctSetRectangle(&r, x, y, w, h);
		WinCopyRectangle(gOff, WinGetDisplayWindow(), &r, x - gDanaOrigin, y, winPaint);
	}
}

void ScreenDanaShow(Boolean right, Int16 y, Int16 h)
{
	if (gLCD.bits) DanaBlit(right ? DANA_SCREEN_W - DANA_RAIL_W : 0, y, DANA_RAIL_W, h);
}

#ifdef DEBUG
void ScreenFailNext(void)
{
	gFailMakes++;
}
#endif

/* Rounds a pixel rectangle out to whole units and keeps it on gS. */
static Boolean Clip(const URect *d, Int16 *x0, Int16 *y0, Int16 *x1, Int16 *y1)
{
	Int16 m = gS.scale - 1;

	*x0 = d->x < 0 ? 0 : d->x & ~m;
	*y0 = d->y < 0 ? 0 : d->y & ~m;
	*x1 = (d->x + d->w + m) & ~m;
	*y1 = (d->y + d->h + m) & ~m;
	if (*x1 > gS.w) *x1 = gS.w;
	if (*y1 > gS.h) *y1 = gS.h;
	return *x1 > *x0 && *y1 > *y0;
}

/* Copies a rectangle of gOff to the display as it is (on 16-bit screens
 * gfx_to16 has to have filled it first). */
static void Blit(const URect *d)
{
	Int16 x0, y0, x1, y1, sc = gS.scale;
	RectangleType r;

	if (!gOff || !gS.bits || !Clip(d, &x0, &y0, &x1, &y1)) return;
	if (gLCD.bits) {
		x0 += DANA_RAIL_W;
		x1 += DANA_RAIL_W;
	}
	if (gLCD.bits || gS.w == DANA_SCREEN_W) {
		DanaBlit(x0, y0, x1 - x0, y1 - y0);
		return;
	}
	if (gSony) {
		RctSetRectangle(&r, x0, y0, x1 - x0, y1 - y0);
		HRWinCopyRectangle(gSonyRef, gOff, WinGetDisplayWindow(), &r, x0, y0, winPaint);
		return;
	}
	RctSetRectangle(&r, x0 / sc, y0 / sc, (x1 - x0) / sc, (y1 - y0) / sc);
	WinCopyRectangle(gOff, WinGetDisplayWindow(), &r, x0 / sc, y0 / sc, winPaint);
}

void ScreenShow(const URect *d)
{
	Int16 x0, y0, x1, y1;

	if (!gOff || !gS.bits || !Clip(d, &x0, &y0, &x1, &y1)) return;
	if (g16) gfx_to16(&gS, gOffBits, gOffRb, x0, y0, x1 - x0, y1 - y0, gLE);
	Blit(d);
}

void ScreenShowAll(void)
{
	URect u;

	if (gLCD.bits) {
		ScreenDanaShow(false, 0, gLCD.h);
		ScreenDanaShow(true, 0, gLCD.h);
	}
	u.x = u.y = 0;
	u.w = gS.w;
	u.h = gS.h;
	ScreenShow(&u);
}

Boolean ScreenKeep(void)
{
	UInt16 err, rb;

	if (!gS.bits) return false;
	if (!gHaveBg) {
		gBgS = gS;
		if (g16) {
			gBg8 = BmpCreate(gS.w, gS.h, 8, NULL, &err);
			if (gBg8) {
				Coord bw, bh;
				BmpGetDimensions(gBg8, &bw, &bh, &rb);
				gBgS.bits = BmpGetBits(gBg8);
			}
		} else {
			gBg = NewWin(gLCD.bits ? gLCD.w : gS.w, gS.h);
			if (gBg) gBgS.bits = WinBits(gBg, &rb);
		}
		if ((!gBg && !gBg8) || !gBgS.bits || rb != gS.rowBytes) {
			ScreenDropKept();
			return false;
		}
		gHaveBg = true;
	}
	gfx_copy(&gBgS, &gS, 0, 0, gS.w, gS.h);
	return true;
}

Surface *ScreenKept(void)
{
	return gHaveBg ? &gBgS : NULL;
}

void ScreenRestore(const URect *d)
{
	if (gHaveBg) gfx_copy(&gS, &gBgS, d->x, d->y, d->w, d->h);
}

void ScreenRestoreAll(void)
{
	if (gHaveBg) gfx_copy(&gS, &gBgS, 0, 0, gS.w, gS.h);
}

#ifdef DEBUG
UInt32 gSwapTicks;
#endif

/* The picture a fade starts from, below 16 bits (ScreenFadeFrom). */
static Surface gFrom;
static Boolean gFromOk;

void ScreenFadeDrop(void)
{
	if (gFrom.bits) MemPtrFree(gFrom.bits);
	gFrom.bits = NULL;
	gFromOk = false;
}

Boolean ScreenFadeFrom(const Surface *src, Int16 y1)
{
	UInt32 n;

	ScreenFadeDrop();
	if (!gS.bits || !gOff || !src || !src->bits || gLCD.bits) return false;
	if (g16) {
		/* gOff holds what the display shows */
		gFromOk = true;
		return true;
	}
	if (y1 > gS.h) y1 = gS.h;
	n = (UInt32)gS.rowBytes * y1;
	gFrom = gS;
	gFrom.h = y1;
	gFrom.bits = MemPtrNew(n);
	if (!gFrom.bits) return false;
	MemMove(gFrom.bits, src->bits, n);
	gFromOk = true;
	return true;
}

UInt32 ScreenDissolve(Int16 y1, UInt16 ms)
{
	URect u;
	UInt32 t0, ts, d, worst, per;
	UInt8 lo = 0, hi, step = 2, k = 0;

	u.x = u.y = 0;
	u.w = gS.w;
	u.h = y1;
	if (!gS.bits || !gOff || !gFromOk || (!g16 && y1 > gFrom.h)) {
		ScreenFadeDrop();
		ScreenShow(&u);
		return 0;
	}
	/* 8 steps of 2 of the 16 dither values. On 16-bit screens gOff holds what
	 * the display shows: each step converts just its pixels of gS into it.
	 * Else the first step swaps in the old picture for 14 of the 16 values,
	 * and each step swaps 2 back, which leaves gS as it was. */
	per = (UInt32)ms * SysTicksPerSecond() / 1000 / 8;
	t0 = TimGetTicks();
	worst = 0;
	while (lo < 16) {
		Int32 wait = (Int32)(t0 + k * per - TimGetTicks());
		if (k && wait > 0) SysTaskDelay(wait);
		hi = (UInt8)(lo + step > 16 ? 16 : lo + step);
		ts = TimGetTicks();
		if (g16) gfx_to16_mask(&gS, gOffBits, gOffRb, 0, 0, gS.w, y1, gLE, lo, hi);
		else if (!k) gfx_swap_mask(&gS, &gFrom, 0, y1, 2, 16);
		else gfx_swap_mask(&gS, &gFrom, 0, y1, lo, hi);
#ifdef DEBUG
		if (!k) gSwapTicks = TimGetTicks() - ts;
#endif
		Blit(&u);
		d = TimGetTicks() - ts;
		if (d > worst) worst = d;
		/* a slow first step: the rest in as many as fit the time, at least 1 */
		if (!k && d > per) {
			UInt32 rest = d < 8 * per ? (8 * per - d) / d : 1;
			if (rest < 1) rest = 1;
			step = (UInt8)(rest >= 7 ? 2 : (14 + rest - 1) / rest);
		}
		lo = hi;
		k++;
	}
	ScreenFadeDrop();
	return worst;
}

/* The spare surface (ScreenSpare). */
static WinHandle gSp;
static BitmapPtr gSp8;
static Surface gSpS;

Surface *ScreenSpare(void)
{
	UInt16 err, rb = 0;

	if (!gS.bits) return NULL;
	if (gSpS.bits) return &gSpS;
	gSpS = gS;
	gSpS.bits = NULL;
	if (g16) {
		gSp8 = BmpCreate(gS.w, gS.h, 8, NULL, &err);
		if (gSp8) {
			Coord bw, bh;
			BmpGetDimensions(gSp8, &bw, &bh, &rb);
			gSpS.bits = BmpGetBits(gSp8);
		}
	} else {
		gSp = NewWin(gLCD.bits ? gLCD.w : gS.w, gS.h);
		if (gSp) gSpS.bits = WinBits(gSp, &rb);
	}
	if (!gSpS.bits || rb != gS.rowBytes) {
		ScreenSpareDrop();
		return NULL;
	}
	return &gSpS;
}

Surface *ScreenSpareHas(void)
{
	return gSpS.bits ? &gSpS : NULL;
}

void ScreenSpareDrop(void)
{
	if (gSp) WinDeleteWindow(gSp, false);
	if (gSp8) BmpDelete(gSp8);
	gSp = NULL;
	gSp8 = NULL;
	gSpS.bits = NULL;
}

UInt32 ScreenDissolveTo(Surface *m, Int16 y1, UInt16 ms, ScreenStep step)
{
	URect u, c;
	UInt32 t0, ts, d, worst, per;
	UInt8 lo = 0, hi, n = 2, k = 0;

	u.x = u.y = 0;
	u.w = gS.w;
	u.h = gS.h;
	if (!gS.bits || !gOff || !m || m->rowBytes != gS.rowBytes || m->h != gS.h) return 0;
	/* the panel at once */
	gfx_copy(&gS, m, 0, y1, gS.w, gS.h - y1);
	c.x = 0;
	c.y = y1;
	c.w = gS.w;
	c.h = gS.h - y1;
	ScreenShow(&c);
	/* 8 steps of 2 of the 16 dither values, each after a frame drawn on m:
	 * its new pixels, and what the frame changed of those already shown */
	per = (UInt32)ms * SysTicksPerSecond() / 1000 / 8;
	t0 = TimGetTicks();
	worst = 0;
	while (lo < 16) {
		Int32 wait = (Int32)(t0 + k * per - TimGetTicks());
		if (k && wait > 0) SysTaskDelay(wait);
		hi = (UInt8)(lo + n > 16 ? 16 : lo + n);
		ts = TimGetTicks();
		c.w = 0;
		if (k && step) step(&c);
		if (g16) {
			gfx_to16_mask(m, gOffBits, gOffRb, 0, 0, gS.w, y1, gLE, lo, hi);
			if (c.w > 0) gfx_to16_mask(m, gOffBits, gOffRb, c.x, c.y, c.x + c.w, c.y + c.h < y1 ? c.y + c.h : y1, gLE, 0, lo);
		} else {
			gfx_copy_mask(&gS, m, 0, 0, gS.w, y1, lo, hi);
			if (c.w > 0) gfx_copy_mask(&gS, m, c.x, c.y, c.x + c.w, c.y + c.h < y1 ? c.y + c.h : y1, 0, lo);
		}
		if (c.w > 0 && c.y + c.h > y1) {
			/* the panel's readout */
			Int16 py = c.y > y1 ? c.y : y1;
			gfx_copy(&gS, m, c.x, py, c.w, c.y + c.h - py);
			if (g16) gfx_to16(&gS, gOffBits, gOffRb, c.x, py, c.w, c.y + c.h - py, gLE);
		}
#ifdef DEBUG
		if (!k) gSwapTicks = TimGetTicks() - ts;
#endif
		Blit(&u);
		d = TimGetTicks() - ts;
		if (d > worst) worst = d;
		/* a slow first step: the rest in 3 */
		if (!k && d > 2 * per) n = 5;
		lo = hi;
		k++;
	}
	gfx_copy(&gS, m, 0, 0, gS.w, gS.h);
	return worst;
}

void ScreenDropKept(void)
{
	if (gBg) WinDeleteWindow(gBg, false);
	if (gBg8) BmpDelete(gBg8);
	gBg = NULL;
	gBg8 = NULL;
	gHaveBg = false;
}

UInt32 ScreenPicType(void)
{
	return gS.depth == 8 ? 'Pic8' : 'Pic2';
}

void ScreenDescribe(char *t)
{
	StrPrintF(t, "%u%s %ux%u%s%s%s%s", (UInt16)gDepth, gSony ? " HR" : gDD ? " DD" : "",
	          (UInt16)gS.w, (UInt16)gS.h, gLE ? " LE" : "", gPins ? " P" : "", gS.half ? " H" : "",
	          ArmOn() ? " A" : "");
}
