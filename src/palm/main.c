/* Palm platform code: launch handling, the screen, input and the event loop.
 * The game draws every screen itself into an offscreen window at the
 * screen's own depth (core gfx.c and ui.c) and copies it to the display. */
#define ALLOW_ACCESS_TO_INTERNALS_OF_WINDOWS
#include <PalmOS.h>
#include <VFSMgr.h>
#include "resource.h"
#include "front.h"
#include "play.h"
#include "screen.h"
#include "sonykeys.h"
#include "audio.h"
#include "sound.h"
#include "records.h"

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

static UInt16 gBgPic;         /* what the kept copy holds: a picture's ID or PIC_SCENE */
#ifdef DEBUG
static UInt32 gShowTicks;
#endif
static Front gFront;
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
#define PIC_SCENE    0xFFFF     /* gBgPic when the kept copy holds the scene */
#define PIC_MAP      0xFFFE     /* or the map of a shot, to fade the next scene in from */
#define FADE_MS      320        /* the fade between the scene and a shot on the map */
#define PREP_MS      60         /* the shot's map drawn this long a frame while the ball flies in the scene */
/* App preferences: the computer golfers' shots setting (one byte, backed
 * up) and the course records (rec_pack). */
#define kCpuPref     3
#define kRecPref     4
#define kShotPref    5
static Records gRecords;
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
static RenderGolfer gGolf;     /* the golfer's frames, kept locked while playing, and the pixels under him */
static MemHandle gGolfH[RENDER_POSES];
static UInt32 gUnderLen;
static UInt32 gMinFree;        /* least free heap while the scene was drawn */
static Boolean gAnim;          /* play wants frames */
static Boolean gFadeBack;      /* a shot is on the map: the next scene fades in over it */
static int gMapF;              /* flags from the frames during the fade to the map */
static Boolean gFadeHold;      /* the next scene frame fades in from the picture ScreenFadeFrom kept */
#ifdef DEBUG
static RenderStats gStats;
static UInt32 gDrawTicks, gMapTicks;
/* the shot view: its map, the whole fade and the slowest fade step, in ticks */
static UInt32 gFadeStep, gGapTicks, gLastFrame;
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

/* The 160 unit square the front screens use, in pixels from the top left. */
static void FrontOrigin(Int16 *x, Int16 *y)
{
	gfx_centre(&gS, 1);
	*x = gS.ox;
	*y = gS.oy;
	gfx_centre(&gS, 0);
}

/* Draws the screen's picture band by band, then the game's text and boxes. */
static void Redraw(void)
{
	UInt32 type = ScreenPicType();
	UInt16 id = front_picture(&gFront), k;
	MemHandle h;
#ifdef DEBUG
	UInt32 t0 = TimGetTicks(), t1, t2;
#endif

	if (!gS.bits) return;
	if (ScreenKept() && gBgPic == id) {
		ScreenRestoreAll();
	} else {
		/* the picture in a 160 unit square in the middle, black round it */
		gfx_fill(&gS, 0, 0, gS.w, gS.h, UIC_BLACK);
		gfx_centre(&gS, 1);
		for (k = 0; (h = DmGetResource(type, id + k)) != NULL; k++) {
			gfx_pic_band(&gS, MemHandleLock(h), MemHandleSize(h));
			MemHandleUnlock(h);
			DmReleaseResource(h);
		}
		gfx_centre(&gS, 0);
		/* Keep a copy if there's room; without one every redraw unpacks. */
		gBgPic = ScreenKeep() ? id : 0;
	}
#ifdef DEBUG
	t1 = TimGetTicks();
#endif
	gfx_centre(&gS, 1);
	front_draw(&gFront, &gS);
	if ((id = front_preview(&gFront)) != 0)
		for (k = 0; (h = DmGetResource(type, id + k)) != NULL; k++) {
			front_preview_band(&gS, MemHandleLock(h), MemHandleSize(h));
			MemHandleUnlock(h);
			DmReleaseResource(h);
		}
	gfx_centre(&gS, 0);
#ifdef DEBUG
	t2 = TimGetTicks();
	if (gFront.screen == SCR_TITLE) {
		char m[32];
		gfx_text(&gS, 0, 0, debugLine, StrLen(debugLine), UIC_WHITE);
		ScreenDescribe(m);
		gfx_text(&gS, 0, 8 * gS.scale, m, StrLen(m), UIC_WHITE);
		AudioDescribe(m);
		gfx_text(&gS, 0, 16 * gS.scale, m, StrLen(m), UIC_WHITE);
	} else if (gFront.screen == SCR_SETTINGS) {
		char m[32];
		AudioDescribe(m);
		gfx_text(&gS, 0, 0, m, StrLen(m), UIC_WHITE);
	}
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
	ScreenShowAll();
}

static Boolean gTxCenter;               /* the centre key has come as vchrTxCenter */

/* Our keys, or KEY_NONE for ones the system should have (Home, power and the like).
 * The four app buttons are ours while the game runs: Select, Map, Scorecard and
 * the shot shape, so the system doesn't launch their apps. */
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
	if (e->data.keyDown.modifiers & commandKeyMask) {
		/* the Jog Dial: turning works as page up and down, pressing as select */
		switch (c) {
		case vchrJogUp: return KEY_UP;
		case vchrJogDown: return KEY_DOWN;
		case vchrJogPush: return KEY_SELECT;
		case vchrJogBack: return KEY_BACK;
		}
	}
	switch (c) {
	case vchrHard1: return KEY_SELECT;
	case vchrHard2: return KEY_MAP;
	case vchrHard3: return KEY_CARD;
	case vchrHard4: return KEY_SHAPE;
	case vchrPageUp: case vchrRockerUp: case chrUpArrow: return KEY_UP;
	case vchrPageDown: case vchrRockerDown: case chrDownArrow: return KEY_DOWN;
	case vchrRockerLeft: case chrLeftArrow: return KEY_LEFT;
	case vchrRockerRight: case chrRightArrow: return KEY_RIGHT;
	case vchrTxCenter:
		gTxCenter = true;
		return KEY_SELECT;
	case vchrRockerCenter:
		/* the TX sends vchrTxCenter and then this one for the same press */
		return gTxCenter ? KEY_NONE : KEY_SELECT;
	case chrLineFeed: case chrCarriageReturn: case chrSpace:
		return KEY_SELECT;
	}
	if (e->data.keyDown.modifiers & commandKeyMask) return KEY_NONE;
	if (c == 'c' || c == 'C' || c == 's' || c == 'S') return KEY_CARD;
	return c >= 32 && c < 256 ? KEY_OTHER : KEY_NONE;
}

/* An app button that woke the device, or came with the powered-on flag: dropped. */
static Boolean WakeKey(const EventType *e)
{
	WChar c = e->data.keyDown.chr;
	return (c == vchrHard1 || c == vchrHard2 || c == vchrHard3 || c == vchrHard4) &&
	       (e->data.keyDown.modifiers & poweredOnKeyMask) != 0;
}

static Boolean gQuitting;

static void Quit(void)
{
	EventType e;

	gQuitting = true;
	MemSet(&e, sizeof(e), 0);
	e.eType = appStopEvent;
	EvtAddEventToQueue(&e);
}

/* Redraws two menu lines from the kept picture and copies just those to the screen. */
static Boolean RedrawLines(void)
{
	UInt8 k, line[2];
	URect u;
	Int16 ox, oy, sc = gS.scale;

#ifdef DEBUG
	UInt32 t0 = TimGetTicks();
	char t[12];
#endif

	if (!ScreenKept() || gBgPic != front_picture(&gFront)) return false;
	line[0] = gFront.oldSel;
	line[1] = gFront.menu.sel;
	FrontOrigin(&ox, &oy);
	for (k = 0; k < 2; k++) {
		ui_menu_line_rect(&gFront.menu, line[k], &u);
		u.x = ox + u.x * sc;
		u.y = oy + u.y * sc;
		u.w *= sc;
		u.h = (u.h + 1) * sc;
		ScreenRestore(&u);
		gfx_centre(&gS, 1);
		ui_menu_draw_line(&gS, &gFront.menu, line[k]);
		gfx_centre(&gS, 0);
		ScreenShow(&u);
	}
#ifdef DEBUG
	/* Ticks for the two lines, bottom left. */
	StrPrintF(t, "L%lu", TimGetTicks() - t0);
	gfx_fill(&gS, 0, gS.h - 8 * gS.scale, 32 * gS.scale, 8 * gS.scale, UIC_BLACK);
	gfx_text(&gS, 0, gS.h - 8 * gS.scale, t, StrLen(t), UIC_WHITE);
	u.x = 0;
	u.y = gS.h - 8 * sc;
	u.w = 32 * sc;
	u.h = 8 * sc;
	ScreenShow(&u);
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

/* Courses on an SD card. A card course is imported whole into RAM
 * (storage heap, not the dynamic heap) when it's picked or resumed and
 * deleted again when the round ends, so a card pulled out mid-round can't
 * hurt a read. All VFS calls are behind HaveVfs(). */
static const char *const kCardDirs[] = { "/PALM/Launcher", "/PALM/Programs/Par386", "/" };
#define CARD_DIRS 3
static Boolean gImported;               /* the open course was imported from a card */
static char gImpName[dmDBNameLength];

static Boolean SEC_MENU HaveVfs(void)
{
	UInt32 v;
	return FtrGet(sysFileCVFSMgr, vfsFtrIDVersion, &v) == errNone;
}

/* Reads the database name of a Par 386 course PDB on the card into name. */
static Boolean SEC_MENU CardCourseName(UInt16 vol, const char *path, char *name)
{
	FileRef f;
	UInt32 type = 0, creator = 0;
	Boolean ok;

	if (VFSFileOpen(vol, path, vfsModeRead, &f) != errNone) return false;
	ok = VFSFileDBInfo(f, name, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &type, &creator, NULL) == errNone &&
	     type == kCourseType && creator == kCreator && StrNCompare(name, kCoursePre, StrLen(kCoursePre)) == 0;
	VFSFileClose(f);
	return ok;
}

/* Calls visit with each Par 386 course PDB on the mounted cards (its
 * database name, the volume and the path) until it gives true. */
typedef Boolean (*CardVisit)(const char *name, UInt16 vol, const char *path, void *ctx);

static char gFileName[256];             /* a directory entry's name */

static void SEC_MENU CardScan(CardVisit visit, void *ctx)
{
	UInt16 vol;
	UInt32 vi = vfsIteratorStart;
	UInt8 d;
	char path[100], name[dmDBNameLength];
	FileRef dir;
	UInt32 it;
	FileInfoType info;
	UInt16 n;

	while (vi != vfsIteratorStop && VFSVolumeEnumerate(&vol, &vi) == errNone) {
		for (d = 0; d < CARD_DIRS; d++) {
			if (VFSFileOpen(vol, kCardDirs[d], vfsModeRead, &dir) != errNone) continue;
			it = vfsIteratorStart;
			info.nameP = gFileName;
			info.nameBufLen = sizeof gFileName;
			while (it != vfsIteratorStop) {
				info.attributes = 0;
				/* an entry that can't be read is skipped, not the rest of the directory */
				if (VFSDirEntryEnumerate(dir, &it, &info) != errNone) {
					if (it == vfsIteratorStop) break;
					continue;
				}
				n = StrLen(gFileName);
				if ((info.attributes & vfsFileAttrDirectory) || n < 5 || StrCaselessCompare(gFileName + n - 4, ".pdb") != 0) continue;
				if (StrLen(kCardDirs[d]) + n + 2 > sizeof path) continue;
				StrCopy(path, kCardDirs[d]);
				if (path[1]) StrCat(path, "/");
				StrCat(path, gFileName);
				if (CardCourseName(vol, path, name) && visit(name, vol, path, ctx)) {
					VFSFileClose(dir);
					return;
				}
			}
			VFSFileClose(dir);
		}
	}
}

/* The imported course's name is kept in an app preference while it's in
 * RAM, so a copy left by a crash or reset is deleted at the next launch. */
#define kImpPref 1

static void SEC_MENU ImpPrefSet(const char *name)
{
	char buf[dmDBNameLength];

	MemSet(buf, sizeof buf, 0);
	if (name) StrCopy(buf, name);
	PrefSetAppPreferences(kCreator, kImpPref, 1, buf, sizeof buf, false);
}

/* Deletes a copy recorded by ImpPrefSet if it's still ours. */
static void SEC_MENU DropLeftover(void)
{
	char buf[dmDBNameLength];
	UInt16 size = sizeof buf;
	LocalID id;
	UInt32 type = 0, creator = 0;

	MemSet(buf, sizeof buf, 0);
	if (PrefGetAppPreferences(kCreator, kImpPref, buf, &size, false) == noPreferenceFound || !buf[0]) return;
	buf[dmDBNameLength - 1] = 0;
	if (StrNCompare(buf, kCoursePre, StrLen(kCoursePre)) == 0 && (id = DmFindDatabase(0, buf)) != 0 &&
	    DmDatabaseInfo(0, id, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &type, &creator) == errNone &&
	    type == kCourseType && creator == kCreator)
		DmDeleteDatabase(0, id);
	ImpPrefSet(NULL);
}

typedef struct { const char *want; Boolean found; Err err; } CardWant;

static Boolean SEC_MENU ImportVisit(const char *name, UInt16 vol, const char *path, void *ctx)
{
	CardWant *w = ctx;
	UInt16 card;
	LocalID id;

	if (StrCompare(name, w->want) != 0) return false;
	w->found = true;
	ImpPrefSet(name);
	w->err = VFSImportDatabaseFromFile(vol, path, &card, &id);
	if (w->err == errNone) {
		gImported = true;
		StrCopy(gImpName, name);
	} else ImpPrefSet(NULL);
	return true;
}

static Boolean SEC_MENU FindVisit(const char *name, UInt16 vol, const char *path, void *ctx)
{
	CardWant *w = ctx;

	if (StrCompare(name, w->want) != 0) return false;
	w->found = true;
	return true;
}

enum { EC_OK, EC_MISSING, EC_NOMEM };

/* EC_OK if the course is in RAM, else imports it from a card. */
static UInt8 SEC_MENU EnsureCourse(const char *name)
{
	CardWant w;

	if (DmFindDatabase(0, name) != 0) return EC_OK;
	if (gImported || !HaveVfs()) return EC_MISSING;
	w.want = name;
	w.found = false;
	w.err = errNone;
	CardScan(ImportVisit, &w);
	if (w.found && w.err == errNone) return EC_OK;
	return w.found && w.err == memErrNotEnoughSpace ? EC_NOMEM : EC_MISSING;
}

/* True if the course is on a card, or no card is in, so a resume can say to
 * insert it. */
static Boolean SEC_MENU CardMayHave(const char *name)
{
	CardWant w;
	UInt16 vol;
	UInt32 vi = vfsIteratorStart;

	if (!HaveVfs()) return false;
	if (VFSVolumeEnumerate(&vol, &vi) != errNone) return true;
	w.want = name;
	w.found = false;
	CardScan(FindVisit, &w);
	return w.found;
}

/* Deletes the imported card course, if any. */
static void SEC_MENU DropImported(void)
{
	LocalID id;

	if (!gImported) return;
	if ((id = DmFindDatabase(0, gImpName)) != 0) DmDeleteDatabase(0, id);
	gImported = false;
	ImpPrefSet(NULL);
}

/* Adds a name to the sorted, capped course list, unless it's there. */
static void SEC_MENU AddCourse(const char *name)
{
	UInt8 i;

	for (i = 0; i < gCourseCount; i++)
		if (StrCompare(gCourseNames[i], name) == 0) return;
	i = gCourseCount;
	if (i == FRONT_COURSES) {
		if (StrCompare(gCourseNames[i - 1], name) <= 0) return;
		i--;
	} else gCourseCount++;
	for (; i > 0 && StrCompare(gCourseNames[i - 1], name) > 0; i--)
		StrCopy(gCourseNames[i], gCourseNames[i - 1]);
	StrCopy(gCourseNames[i], name);
}

static Boolean SEC_MENU ListVisit(const char *name, UInt16 vol, const char *path, void *ctx)
{
	/* a course in RAM wins */
	if (DmFindDatabase(0, name) == 0) AddCourse(name);
	return false;
}

/* Lets go of the golfer's frames and the pixels kept under him. */
static void GolferFree(void)
{
	UInt16 k;

	for (k = 0; k < RENDER_POSES; k++) {
		if (gGolfH[k]) {
			MemHandleUnlock(gGolfH[k]);
			DmReleaseResource(gGolfH[k]);
		}
		gGolfH[k] = NULL;
		gGolf.pose[k].p = NULL;
		gGolf.pose[k].len = 0;
	}
	if (gGolf.under) MemPtrFree(gGolf.under);
	gGolf.under = NULL;
	gGolf.w = 0;
	gUnderLen = 0;
}

/* Locks the golfer's frames for scale sc (Spr8 2000 + 10 * pose + sc - 1)
 * and makes the buffer for the pixels under him. They stay while playing,
 * so his pose can change over the kept scene. Without the buffer the pose
 * stays at address. */
static void GolferLock(UInt16 sc)
{
	UInt16 k;
	UInt32 n = render_under_size(&gS);

	for (k = 0; k < RENDER_POSES; k++) {
		MemHandle h;
		if (gGolfH[k]) {
			MemHandleUnlock(gGolfH[k]);
			DmReleaseResource(gGolfH[k]);
		}
		gGolfH[k] = NULL;
		gGolf.pose[k].p = NULL;
		gGolf.pose[k].len = 0;
		h = DmGetResource('Spr8', 2000 + 10 * k + sc - 1);
		if (!h) continue;
		gGolfH[k] = h;
		gGolf.pose[k].p = MemHandleLock(h);
		gGolf.pose[k].len = MemHandleSize(h);
	}
	if (gUnderLen != n) {
		if (gGolf.under) MemPtrFree(gGolf.under);
		gGolf.under = MemPtrNew(n);
		gUnderLen = gGolf.under ? n : 0;
	}
}

static void PlayEnd(void)
{
	LocalID id;

	ScreenSpareDrop();
	if (gGridH) MemHandleUnlock(gGridH);
	if (gHoleH) MemHandleUnlock(gHoleH);
	gGridH = gHoleH = NULL;
	if (gScratchDb) DmCloseDatabase(gScratchDb);
	if (gCourseDb) DmCloseDatabase(gCourseDb);
	gScratchDb = gCourseDb = 0;
	if ((id = DmFindDatabase(0, kScratchName)) != 0) DmDeleteDatabase(0, id);
	DropImported();
	GolferFree();
	gPlaying = false;
}

/* Each course's record line on the courses screen. */
static void SEC_MENU RecordsShow(void)
{
	UInt8 k;
	for (k = 0; k < gCourseCount; k++) {
		UInt8 t;
		for (t = 0; t < COURSE_MAX_TEES; t++) {
			const CourseRecord *c = rec_find(&gRecords, gCourseNames[k], t);
			front_record(&gFront, k, t, c ? c->strokes : 0, c ? c->to_par : 0);
		}
	}
}

static void SEC_MENU RecordsLoad(void)
{
	UInt8 *buf = MemPtrNew(REC_SAVE_MAX);
	UInt16 size = REC_SAVE_MAX;

	rec_init(&gRecords);
	if (!buf) return;
	if (PrefGetAppPreferences(kCreator, kRecPref, buf, &size, true) != noPreferenceFound && size <= REC_SAVE_MAX)
		rec_unpack(&gRecords, buf, size);
	MemPtrFree(buf);
}

static void SEC_MENU RecordsSave(void)
{
	UInt8 *buf = MemPtrNew(REC_SAVE_MAX);
	if (!buf) return;
	PrefSetAppPreferences(kCreator, kRecPref, 1, buf, rec_pack(&gRecords, buf), true);
	MemPtrFree(buf);
	RecordsShow();
}

static void SEC_MENU DeleteSave(void)
{
	LocalID id = DmFindDatabase(0, kSaveName);
	if (id) DmDeleteDatabase(0, id);
}

/* Saves the round, as it will be after any shot in the air lands, or
 * deletes the save once the round is over. */
static void SEC_MENU SaveRound(void)
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
		/* a finished round may set course records; a second save of it sets none */
		if (rec_round(&gRecords, &gRound, gRound.tee)) RecordsSave();
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
static Boolean SEC_MENU CourseHeader(DmOpenRef db, Course *c)
{
	MemHandle h = DmQueryRecord(db, 0);
	Boolean ok;

	if (!h) return false;
	ok = course_parse(c, MemHandleLock(h), MemHandleSize(h)) == HOLE_OK && DmNumRecords(db) > c->hole_count;
	MemHandleUnlock(h);
	return ok;
}

enum { LS_OK, LS_NONE, LS_CHANGED, LS_MISSING, LS_NOMEM };

/* Reads the saved round into r. LS_NONE if there's none or it's of another
 * version or broken, LS_CHANGED if its course's holes or pars changed (both
 * mean the save is no use), LS_MISSING if the course isn't in RAM or on a
 * card in, LS_NOMEM if it couldn't be imported for lack of memory. With
 * full a card course is imported and checked and stays imported; without,
 * it's only looked for on a card. */
static UInt8 SEC_MENU LoadSave(Round *r, Boolean full)
{
	LocalID id = DmFindDatabase(0, kSaveName);
	DmOpenRef db;
	MemHandle h;
	Course c;
	Boolean ok = false;
	UInt8 k, e;

	if (!id || (db = DmOpenDatabase(0, id, dmModeReadOnly)) == 0) return LS_NONE;
	if ((h = DmQueryRecord(db, 0)) != NULL) {
		ok = rules_unpack(r, MemHandleLock(h), MemHandleSize(h)) == 0 && !r->over;
		MemHandleUnlock(h);
	}
	DmCloseDatabase(db);
	if (!ok) return LS_NONE;
	if (!full && DmFindDatabase(0, r->course) == 0) return CardMayHave(r->course) ? LS_OK : LS_MISSING;
	if ((e = EnsureCourse(r->course)) != EC_OK) return e == EC_NOMEM ? LS_NOMEM : LS_MISSING;
	if ((id = DmFindDatabase(0, r->course)) == 0 || (db = DmOpenDatabase(0, id, dmModeReadOnly)) == 0) {
		DropImported();
		return LS_MISSING;
	}
	ok = CourseHeader(db, &c) && c.hole_count == r->holes;
	for (k = 0; ok && k < r->holes; k++)
		if (c.par[k] != r->pars[k]) ok = false;
	DmCloseDatabase(db);
	if (!ok) DropImported();
	return ok ? LS_OK : LS_CHANGED;
}

/* Hole n (from 1) of the open course: parses it, keeping its record
 * locked, and draws its surface grid into record 0 of the scratch database
 * in place of the last hole's. */
static Boolean SEC_MENU LoadHole(UInt16 n)
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
	gHole.wind = gCourseHdr.wind;
	gHole.green = gCourseHdr.green;
	hole_use_tee(&gHole, gRound.tee);
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
/* Why PlayStart failed. */
enum { PF_MEM, PF_SAVE, PF_MISSING, PF_NOCOURSE };
static UInt8 gFail;

static Boolean SEC_MENU PlayStart(Boolean resume)
{
	LocalID id;
	Course c;
	UInt32 seed;

	gFail = PF_MEM;
	if (resume) {
		switch (LoadSave(&gRound, true)) {
		case LS_OK: break;
		case LS_NOMEM: gFail = PF_MEM; return false;
		case LS_MISSING: gFail = PF_MISSING; return false;
		default: gFail = PF_SAVE; return false;
		}
	} else {
		if (gFront.course >= gCourseCount) { gFail = PF_NOCOURSE; return false; }
		switch (EnsureCourse(gCourseNames[gFront.course])) {
		case EC_OK: break;
		case EC_NOMEM: gFail = PF_MEM; return false;
		default: gFail = PF_NOCOURSE; return false;
		}
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
		/* the range is played on the first par 5, the green on the first hole */
		{
			UInt8 h = 0, k;
			if (gFront.mode == MODE_RANGE)
				for (k = 0; k < c.hole_count; k++)
					if (c.par[k] >= 5) { h = k; break; }
			rules_mode(&gRound, gFront.mode, gFront.cpu, h);
		}
		/* practice from the first tee set */
		/* a card course's tee sets are known now: the players line and the record show the set played */
		front_course_tees(&gFront, gFront.course, &c);
		if (gFront.tee >= c.tee_count) gFront.tee = (UInt8)(c.tee_count - 1);
		if (!MODE_PRACTICE(gFront.mode)) gRound.tee = gFront.tee;
	}
	if ((id = DmFindDatabase(0, gRound.course)) == 0 || (gCourseDb = DmOpenDatabase(0, id, dmModeReadOnly)) == 0)
		return false;
	if (!CourseHeader(gCourseDb, &gCourseHdr)) return false;
	if ((id = DmFindDatabase(0, kScratchName)) != 0) DmDeleteDatabase(0, id);
	if (DmCreateDatabase(0, kScratchName, kCreator, 'Tmp ', false) != errNone) return false;
	if ((id = DmFindDatabase(0, kScratchName)) == 0) return false;
	if ((gScratchDb = DmOpenDatabase(0, id, dmModeReadWrite)) == 0) return false;
	if (!LoadHole(gRound.hole + 1)) return false;
	gPlay.quick = gFront.quick;
	gPlay.mapview = gFront.shotmap;
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

/* Copies the scene to the kept copy (screen.c), so overlays can be taken
 * off and the map put back without drawing it again. */
/* Rows of gS above the panel, in pixels. */
static Int16 ViewRows(void)
{
	return (Int16)(gS.h - RENDER_STATUS * gS.scale);
}

/* Fades gS in over the screen from the picture ScreenFadeFrom kept
 * (ScreenDissolve), timed in debug builds. */
static void Fade(void)
{
#ifdef DEBUG
	gFadeStep = ScreenDissolve(ViewRows(), FADE_MS);
#else
	ScreenDissolve(ViewRows(), FADE_MS);
#endif
}

static void KeepScene(void)
{
	/* after a shot on the map, the kept copy holds the map (see PlayAct): the
	 * first frame of the new scene, golfer and all, fades in over it (PlayFrame) */
	if (gFadeBack && gBgPic == PIC_MAP && ScreenFadeFrom(ScreenKept(), ViewRows())) gFadeHold = true;
	gFadeBack = false;
	ScreenSpareDrop();
	gBgPic = ScreenKeep() ? PIC_SCENE : 0;
}

/* Time for the play code, in ms. */
static UInt32 NowMs(void)
{
	UInt32 t = TimGetTicks(), tps = SysTicksPerSecond();
	return (t / tps) * 1000 + (t % tps) * 1000 / tps;
}

static void SceneRender(void);
static void PlayAct(int f);

/* Plays what the core queued; the birds only on the scene while playing. */
static void Sound(void)
{
	AudioTick(gPlaying && gPlay.view == PLAY_SCENE && snd_ambient_on());
}

/* Shows the strip saying whose turn it is while the scene draws. */
static void TurnStrip(void)
{
	URect u;

	if (!gS.bits) return;
	play_turn(&gS, &gPlay);
	u.x = 0;
	u.y = gS.h - RENDER_STATUS * gS.scale;
	u.w = gS.w;
	u.h = RENDER_STATUS * gS.scale;
	ScreenShow(&u);
}

#ifdef DEBUG
/* Notes the physics time when a click (or the meter running out) started the flight or putt. */
static void SimTime(UInt8 before, UInt32 t0)
{
	if ((before == PS_DOWN || before == PS_UP) && gPlay.state == PS_FLY) gSimTicks = TimGetTicks() - t0;
}
#endif

#ifdef DEBUG
/* The longest time between frames in the first 1.5 s of a flight (the shot view's switch). */
static UInt32 gSwitchTicks, gLastPrep, gLast3D, gGapPrep, gGap3D;
static void FrameMark(void)
{
	UInt32 t = TimGetTicks();
	if (gPlay.state == PS_FLY && gPlay.shown >= -1 && gLastFrame && (Int32)(NowMs() - gPlay.tf) < 1500 &&
	    t - gLastFrame > gGapTicks) {
		gGapTicks = t - gLastFrame;
		gGapPrep = gLastPrep;
		gGap3D = gLast3D;
	}
	gLastFrame = t;
}
#endif

/* Draws the next slice of the shot's map on the spare surface, sized to
 * take about PREP_MS. With no memory for it the shot stays in the scene. */
static void MapPrep(void)
{
	static Int16 rows;
	static UInt32 done, spent;
	Surface *m = ScreenSpare();
	UInt32 t0, d, want = (UInt32)PREP_MS * SysTicksPerSecond() / 1000, r;

	if (!m) {
		gPlay.mprep = 3;
		return;
	}
	if (!gPlay.mprep) {
		rows = (Int16)(4 * gS.scale);
		done = spent = 0;
	}
	t0 = TimGetTicks();
	play_map_prep(m, &gPlay, rows);
	d = TimGetTicks() - t0;
#ifdef DEBUG
	gLastPrep = d;
#endif
	/* the next slice from the rows a tick so far (ticks can be 10 ms), at most twice as big */
	done += (UInt32)rows;
	spent += d;
	r = spent ? done * want / spent : (UInt32)rows * 2;
	if (r > (UInt32)rows * 2) r = (UInt32)rows * 2;
	rows = (Int16)(r < 1 ? 1 : r > (UInt32)(64 * gS.scale) ? 64 * gS.scale : r);
}

/* A frame of the shot during the fade to the map, drawn on the spare surface. */
static void MapStep(URect *c)
{
	int f;
	Surface *m = ScreenSpareHas();
	c->w = 0;
	if (!m || (gMapF & ~(PL_ANIM | PL_DIRTY))) return;
#ifdef DEBUG
	FrameMark();
#endif
	f = play_frame(&gPlay, m, NULL, NowMs());
	gMapF |= f;
	if (f & PL_DIRTY) *c = gPlay.dirty;
}

/* Lets play move the meter or the ball on and draw over the scene, and copies what changed to the screen. */
static void PlayFrame(void)
{
	int f;
#ifdef DEBUG
	UInt8 st = gPlay.state;
	UInt32 t0 = TimGetTicks();
#endif

	gAnim = false;
	if (!gPlaying || !gS.bits) {
		gFadeHold = false;
		ScreenFadeDrop();
		return;
	}
	if (gPlay.view == PLAY_MAP) {
		/* a shot on the map, or a computer golfer's result there: on to whatever comes next */
#ifdef DEBUG
		FrameMark();
#endif
		f = play_frame(&gPlay, &gS, NULL, NowMs());
		if (f & PL_DIRTY) ScreenShow(&gPlay.dirty);
		f &= ~PL_DIRTY;
		if (f & ~PL_ANIM) PlayAct(f);
		else gAnim = (f & PL_ANIM) != 0;
		return;
	}
	if (gPlay.view != PLAY_SCENE) return;
	gPlay.golf = &gGolf;
#ifdef DEBUG
	if (gPlay.state == PS_FLY && gPlay.shown == -2) {
		gGapTicks = 0;
		gLastFrame = 0;
	}
	FrameMark();
#endif
	f = play_frame(&gPlay, &gS, gBgPic == PIC_SCENE ? ScreenKept() : NULL, NowMs());
	if (gFadeHold) {
		/* the first frame after a shot on the map fades in */
		gFadeHold = false;
		if (f & (PL_BACK | PL_CARD | PL_MAP | PL_NEXT | PL_SCENE | PL_RENDER)) ScreenFadeDrop();
		else Fade();
	}
	if (f & (PL_BACK | PL_CARD | PL_MAP | PL_NEXT | PL_SCENE)) {
		/* the end of a hole, a computer golfer's turn on the map, or the range's next ball */
		PlayAct(f);
		return;
	}
	if (f & PL_RENDER) {
		/* the next shot, or aimed off the side: SceneRender comes back here once it's drawn */
		if (f & PL_TURN) TurnStrip();
		SceneRender();
		return;
	}
	if (f & PL_DIRTY) ScreenShow(&gPlay.dirty);
	gAnim = (f & PL_ANIM) != 0;
#ifdef DEBUG
	gLast3D = TimGetTicks() - t0;
	gLastPrep = 0;
#endif
	if (play_map_wanted(&gPlay)) MapPrep();
	else if (gPlay.mprep == 3) ScreenSpareDrop();    /* the shot stays in the scene */
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
	/* none over a shot on the map; the next map shows its times */
	if (gPlay.onmap) return;
	StrPrintF(t, "m%lu %luK u%luK", gMapTicks * 1000 / tps, gMinFree >> 10,
	          gHeapStart > gMinFree ? (gHeapStart - gMinFree) >> 10 : 0);
	gfx_fill(&gS, 0, 0, (Int16)(StrLen(t) * 8 * gS.scale), 8 * gS.scale, UIC_BLACK);
	gfx_text(&gS, 0, 0, t, StrLen(t), UIC_WHITE);
	StrPrintF(t, "S%lu M%lu F%lu P%lu", gDrawTicks * 1000 / tps,
	          gMtrN ? gMtrTicks * 10000 / tps / gMtrN : 0,
	          gFlyN ? gFlyTicks * 10000 / tps / gFlyN : 0, gSimTicks * 1000 / tps);
	gfx_fill(&gS, 0, 8 * gS.scale, (Int16)(StrLen(t) * 8 * gS.scale), 8 * gS.scale, UIC_BLACK);
	gfx_text(&gS, 0, 8 * gS.scale, t, StrLen(t), UIC_WHITE);
	/* the last shot on the map: the longest gap between frames in its first 1.5 s, the scene frame and map
	 * slice before that gap, the switch (from its frame to the map's first frame) and the last fade's slowest step, in ms */
	StrPrintF(t, "g%lu %lu+%lu w%lu d%lu", gGapTicks * 1000 / tps, gGap3D * 1000 / tps,
	          gGapPrep * 1000 / tps, gSwitchTicks * 1000 / tps, gFadeStep * 1000 / tps);
	gfx_fill(&gS, 0, 16 * gS.scale, (Int16)(StrLen(t) * 8 * gS.scale), 8 * gS.scale, UIC_BLACK);
	gfx_text(&gS, 0, 16 * gS.scale, t, StrLen(t), UIC_WHITE);
#else
	play_status(&gS, &gPlay, NULL);
#endif
}

/* Draws the scene with the art resources locked and the work memory
 * borrowed for the draw, or the green view, which needs neither. */
static void SceneRender(void)
{
	RenderArt art;
	MemHandle hs[RENDER_TYPES * RENDER_VARIANTS * RENDER_SIZES + 3];
	UInt16 n = 0, t, v, k, sc, s = gCourseHdr.scenery;
	void *work;
	UInt32 fr;
#ifdef DEBUG
	UInt32 t0;
#endif

	/* a shot's map that wasn't used isn't kept through the draw */
	ScreenSpareDrop();
	if (!gS.bits) return;
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
	/* the course style's sprites (tools/mkstyle.py), else the shared ones;
	 * a variant the style doesn't have is left out and render.c uses the first */
	for (t = 0; t < RENDER_TYPES; t++)
		for (v = 0; v < RENDER_VARIANTS; v++)
			for (k = 0; k < RENDER_SIZES; k++) {
				if (s) LockArt(&art.spr[t][v][k], 'Spr8', 3000 + 200 * s + 10 * t + 2 * v + k, hs, &n);
				if (!art.spr[t][v][k].p && !v) LockArt(&art.spr[t][v][k], 'Spr8', 1000 + t * 10 + k, hs, &n);
			}
	/* the golfer and backdrop for the scale the scene is drawn at */
	sc = gS.half ? 1 : gS.scale;
	GolferLock(sc);
	art.golfer = &gGolf;
	gPlay.golf = &gGolf;
	/* the course's backdrop, or the first one if it's not in this build */
	LockArt(&art.backdrop, 'Bkd8', 1000 + 10 * gCourseHdr.scenery + sc - 1, hs, &n);
	if (!art.backdrop.p) LockArt(&art.backdrop, 'Bkd8', 1000 + sc - 1, hs, &n);
	gGolf.w = 0;    /* no golfer box until render_scene sets one */
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

/* The first view of a turn: the scene with the turn panel while it draws,
 * or a quick computer golfer's result on the map. */
static void PlayBegin(void)
{
	if (gPlay.view == PLAY_MAP) {
		PlayAct(PL_MAP | PL_ANIM);
		return;
	}
	TurnStrip();
	SceneRender();
}

static void PlayAct(int f)
{
	/* sounds start before any long draw */
	Sound();
	if (!gS.bits) return;
	if (f & (PL_BACK | PL_NEXT | PL_CARD)) gFadeBack = false;
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
		PlayBegin();
	} else if (f & PL_CARD) {
		/* over the kept scene; saved when it opens at the end of a hole */
		gAnim = false;
		if (ScreenKept() && gBgPic == PIC_SCENE) ScreenRestoreAll();
		play_card(&gS, &gPlay);
		ScreenShowAll();
		if (gPlay.card != CARD_VIEW) SaveRound();
	} else if ((f & PL_MAP) && gPlay.onmap && !gFadeBack && gPlay.mprep == 2 && ScreenSpareHas()) {
		/* a shot going over to the map: its map is ready on the spare surface;
		 * the ball flies on over it while it fades in */
		Surface *m = ScreenSpareHas();
		gAnim = false;
		gMapF = play_frame(&gPlay, m, NULL, NowMs());
#ifdef DEBUG
		gSwitchTicks = TimGetTicks() - gLastFrame;
#endif
#ifdef DEBUG
		gFadeStep = ScreenDissolveTo(m, ViewRows(), FADE_MS, MapStep);
		FrameMark();
#else
		ScreenDissolveTo(m, ViewRows(), FADE_MS, MapStep);
#endif
		ScreenSpareDrop();
		gFadeBack = true;
		f = gMapF & ~PL_DIRTY;
		if (f & ~PL_ANIM) PlayAct(f);
		else gAnim = (f & PL_ANIM) != 0;
	} else if (f & PL_MAP) {
#ifdef DEBUG
		UInt32 t0 = TimGetTicks();
#endif
		gAnim = false;
		ScreenSpareDrop();
		play_map(&gS, &gPlay);
#ifdef DEBUG
		if (!gPlay.onmap) gMapTicks = TimGetTicks() - t0;
#endif
		PlayStatus();
		ScreenShowAll();
		gFadeBack = gPlay.onmap;
		gAnim = (f & PL_ANIM) != 0;
	} else if (f & PL_PANEL) {
		URect u;
		PlayStatus();
		u.x = 0;
		u.y = gS.h - RENDER_STATUS * gS.scale;
		u.w = gS.w;
		u.h = RENDER_STATUS * gS.scale;
		ScreenShow(&u);
		gAnim = (f & PL_ANIM) != 0;
	} else if ((f & PL_SCENE) && ScreenKept() && gBgPic == PIC_SCENE) {
		if (gFadeBack) {
			/* the range's next ball after a shot on the map: its first frame fades in (PlayFrame) */
			if (ScreenFadeFrom(&gS, ViewRows())) gFadeHold = true;
			gFadeBack = false;
		}
		ScreenRestoreAll();
		PlayFrame();
	} else if (f & (PL_SCENE | PL_RENDER)) {
		/* the map stays on the screen while the scene draws: keep it to fade from */
		if (gFadeBack) gBgPic = ScreenKeep() ? PIC_MAP : 0;
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

static char gMsg[48];                  /* the message dialog keeps its text */

static void Act(int r)
{
	Sound();
	if (r & FR_SOUND) AudioSetMode(gFront.sound);
	if (r & FR_CPU) PrefSetAppPreferences(kCreator, kCpuPref, 1, &gFront.quick, 1, true);
	if (r & FR_SHOT) PrefSetAppPreferences(kCreator, kShotPref, 1, &gFront.shotmap, 1, true);
	if (r & (FR_PLAY | FR_CONTINUE)) {
		if (PlayStart((r & FR_CONTINUE) != 0)) {
			PlayBegin();
			return;
		}
		PlayEnd();
		if (gFail == PF_SAVE) {
			DeleteSave();
			front_clubhouse(&gFront, 0);
			front_message(&gFront, "Can't continue", "The saved round's\ncourse has changed.");
		} else if (gFail == PF_MISSING) {
			/* the save stays for when the card is back */
			StrCopy(gMsg, "Insert the card\nwith ");
			StrCat(gMsg, gRound.course + StrLen(kCoursePre));
			front_clubhouse(&gFront, 1);
			front_message(&gFront, "Course missing", gMsg);
		} else if (gFail == PF_NOCOURSE) {
			front_clubhouse(&gFront, LoadSave(&gRound, false) == LS_OK);
			front_message(&gFront, "No course", "Install a course\nPDB and try again.");
		} else {
			front_clubhouse(&gFront, LoadSave(&gRound, false) == LS_OK);
			front_message(&gFront, "Out of memory", "Free some storage\nand try again.");
		}
		Redraw();
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

/* No memory for even the smallest screen: the round is kept to continue
 * and the app closes. */
static void ScreenLost(void)
{
	if (gQuitting) return;
	gAnim = false;
	gBgPic = 0;
	if (gPlaying) {
		SaveRound();
		PlayEnd();
	}
	FrmAlert(MemoryAlert);
	Quit();
}

/* The display changed size (the input area or a turn): the buffers are made
 * again at the new size and the screen drawn again. The map or card is drawn
 * again by itself; the scene is drawn when it closes. */
static void Resized(void)
{
	Int16 r;

	if (gQuitting) return;
	r = ScreenResize();
	if (r == SCREEN_FAILED) {
		ScreenLost();
		return;
	}
	if (r == SCREEN_SAME) {
		ScreenShowAll();
		return;
	}
	gBgPic = 0;
	if (!gPlaying) {
		Redraw();
		return;
	}
	gAnim = false;
	play_resized(&gPlay);
	if (gPlay.view == PLAY_MAP) {
		/* a shot on the map flies on */
		PlayAct(gPlay.onmap ? PL_MAP | PL_ANIM : PL_MAP);
	} else if (gPlay.view == PLAY_CARD) {
		gfx_fill(&gS, 0, 0, gS.w, gS.h, UIC_BLACK);
		play_card(&gS, &gPlay);
		ScreenShowAll();
	} else {
		SceneRender();
	}
}

static Boolean MainFormHandleEvent(EventType *e)
{
	switch (e->eType) {
	case frmOpenEvent:
		FrmDrawForm(FrmGetActiveForm());
		/* the input area may have been closed before launch, so no
		 * winDisplayChangedEvent comes to fit the form */
		if (ScreenResize() == SCREEN_FAILED) {
			ScreenLost();
			return true;
		}
		gBgPic = 0;
		Redraw();
		return true;
	case frmUpdateEvent:
		ScreenShowAll();
		return true;
	case winDisplayChangedEvent:
		Resized();
		return true;
	case penDownEvent:
		if (!gS.bits) return true;
#ifdef DEBUG
		/* a tap in the bottom left corner makes the next screen buffers
		 * fail once more, for testing ScreenResize */
		if (e->screenX < 8 && e->screenY >= gS.lh - 8) {
			ScreenFailNext();
			return true;
		}
		/* a tap in the top left corner skips the hole, as n does */
		if (gPlaying && e->screenX < 8 && e->screenY < 8) {
			SkipHole();
			return true;
		}
		/* and the top right corner puts the ball on the green */
		if (gPlaying && e->screenX >= gS.lw - 8 && e->screenY < 8 && gPlay.view == PLAY_SCENE) {
			PlayAct(play_debug_green(&gPlay));
			return true;
		}
#endif
		if (gPlaying) {
			PlayInput(true, e->screenX, e->screenY, 0);
		} else {
			Int16 ox, oy;
			FrontOrigin(&ox, &oy);
			Act(front_tap(&gFront, e->screenX - ox / gS.scale, e->screenY - oy / gS.scale));
		}
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
		if (id == MainForm) {
			FrmSetEventHandler(form, MainFormHandleEvent);
			ScreenFormInit(form);
		}
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
		/* a tick between frames while the meter or the ball moves, or until the next step of a sound */
		Sound();
		EvtGetEvent(&e, gAnim ? 1 : AudioWait());
		if (gAnim) PlayFrame();
		if (e.eType == nilEvent && gPlaying) continue;
		/* with no screen buffers only the system gets keys */
		if (e.eType == keyDownEvent && !gS.bits) {
			SysHandleEvent(&e);
			continue;
		}
		/* the press that wakes the device isn't ours: no select or swing from it */
		if (e.eType == keyDownEvent && WakeKey(&e)) continue;
		if (e.eType == keyDownEvent && gPlaying) {
			/* the Menu button and backspace go back to the clubhouse */
			WChar c = e.data.keyDown.chr;
#ifdef DEBUG
			if (c == 'n' || c == 'N') {
				SkipHole();
				continue;
			}
			if ((c == 'g' || c == 'G') && gPlay.view == PLAY_SCENE) {
				PlayAct(play_debug_green(&gPlay));
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

/* Finds the installed course databases (type Crse, ours, in RAM or on a card) and gives their
 * names, less "Par386 ", to the clubhouse, sorted, the first picked. */
static void SEC_MENU FindCourses(void)
{
	DmSearchStateType st;
	UInt16 card;
	LocalID id;
	Boolean first = true;
	char name[dmDBNameLength];
	const char *shown[FRONT_COURSES];
	UInt8 k, pre = StrLen(kCoursePre), scenery[FRONT_COURSES];
	DmOpenRef db;
	Course c;

	gCourseCount = 0;
	while (DmGetNextDatabaseByTypeCreator(first, &st, kCourseType, kCreator, false, &card, &id) == errNone) {
		first = false;
		if (card != 0 || DmDatabaseInfo(card, id, name, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) != errNone)
			continue;
		if (StrNCompare(name, kCoursePre, pre) != 0 || !name[pre]) continue;
		AddCourse(name);
	}
	if (HaveVfs()) CardScan(ListVisit, NULL);
	/* each one's scenery for its preview; a card course isn't read until it's played */
	for (k = 0; k < gCourseCount; k++) {
		shown[k] = gCourseNames[k] + pre;
		scenery[k] = SCENERY_UNKNOWN;
		front_course_tees(&gFront, k, NULL);
		if ((id = DmFindDatabase(0, gCourseNames[k])) != 0 && (db = DmOpenDatabase(0, id, dmModeReadOnly)) != NULL) {
			if (CourseHeader(db, &c)) {
				scenery[k] = c.scenery;
				front_course_tees(&gFront, k, &c);
			}
			DmCloseDatabase(db);
		}
	}
	front_courses(&gFront, shown, scenery, gCourseCount, 0);
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
	gFront.sound = AudioOpen();
	snd_set_hook(Sound);
	DropLeftover();
	{
		UInt8 q = 1;
		UInt16 size = 1;
		if (PrefGetAppPreferences(kCreator, kCpuPref, &q, &size, true) == noPreferenceFound || size != 1 || q > 1) q = 1;
		gFront.quick = q;
		q = 1;
		size = 1;
		if (PrefGetAppPreferences(kCreator, kShotPref, &q, &size, true) == noPreferenceFound || size != 1 || q > 1) q = 1;
		gFront.shotmap = q;
	}
	RecordsLoad();
	FindCourses();
	RecordsShow();
	front_clubhouse(&gFront, LoadSave(&gRound, false) == LS_OK);
	gFront.screen = SCR_TITLE;
	FrmGotoForm(MainForm);
	EventLoop();
	FrmCloseAllForms();
	/* leaving mid-round saves it */
	if (gPlaying) {
		SaveRound();
		PlayEnd();
	}
	snd_set_hook(NULL);
	AudioClose();
	ScreenClose();
	return 0;
}
