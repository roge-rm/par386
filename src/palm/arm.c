/* Calls the ARM code in the armc resources through PceNativeCall: 1000 the
 * 16-bit conversion, 1001 the renderer's loops. The To16 job is passed as
 * it is (big endian; src/arm/to16arm.c reads it so) with pal swapped for a
 * copy of the palette in the ARM's byte order. The renderer's jobs go as
 * they are, behind the op (src/arm/terraarm.c). */
#include <PalmOS.h>
#include <stddef.h>
#include <PceNativeCall.h>
#include "arm.h"
#include "gfx.h"
#include "to16.h"
#include "terra.h"
#include "render.h"

#define kArmType 'armc'
#define kArmTo16 1000
#define kArmTerra 1001

static MemHandle gArmH, gTerraH;
static NativeFuncType *gArmTo16, *gArmTerra;
static UInt8 *gPalLE;

/* m68k-palmos-gcc lays To16 out as to16arm.c reads it */
#define TO16_AT(f, o) (offsetof(To16, f) == (o))
typedef char To16Size[sizeof(To16) == TO16_SIZE ? 1 : -1];
typedef char To16Offs[TO16_AT(d, TO16_D) && TO16_AT(pal, TO16_PAL) && TO16_AT(dith, TO16_DITH)
	&& TO16_AT(srb, TO16_SRB) && TO16_AT(x0, TO16_X0) && TO16_AT(y1, TO16_Y1)
	&& TO16_AT(le, TO16_LE) && TO16_AT(hi, TO16_HI) && TO16_AT(width, TO16_WIDTH)
	&& TO16_AT(smooth_y1, TO16_SMOOTH_Y1) ? 1 : -1];

/* and the renderer's jobs as terraarm.c reads them */
#define AT(s, f, o) (offsetof(s, f) == (o))
typedef char TerraRowOffs[AT(TerraRow, grey, TR_GREY) && AT(TerraRow, dpys, TR_DPYS) && AT(TerraRow, rb, TR_RB)
	&& AT(TerraRow, gs, TR_GS) && AT(TerraRow, tq, TR_TQ) && AT(TerraRow, wlv, TR_WLV) && AT(TerraRow, ymax, TR_YMAX)
	&& AT(TerraRow, lg, TR_LG) && AT(TerraRow, hm, TR_HM) && AT(TerraRow, material, TR_MATERIAL)
	&& AT(TerraRow, cut, TR_CUT) && AT(TerraRow, prior_material, TR_PRIOR_MATERIAL)
	&& AT(TerraRow, prior_y, TR_PRIOR_Y) && AT(TerraRow, prior_sand_px, TR_PRIOR_SAND)
	&& AT(TerraRow, sand_relief, TR_SAND_RELIEF) && AT(TerraRow, sand_light, TR_SAND_LIGHT)
	&& AT(TerraRow, prior_relief, TR_PRIOR_RELIEF)
	&& AT(TerraRow, spans, TR_SIZE) ? 1 : -1];
typedef char TerraTrisOffs[AT(TerraTris, grey, TT_GREY) && AT(TerraTris, rb, TT_RB) && AT(TerraTris, n, TT_N)
	&& AT(TerraTris, lg, TT_LG) && AT(TerraTris, turf, TT_TURF) && AT(TerraTris, noise, TT_NOISE)
	&& AT(TerraTris, mow_phase, TT_MOW_PHASE) && AT(TerraTris, reflection, TT_REFLECTION) && AT(TerraTurf, depth, 18)
	&& sizeof(TerraTris) == TT_SIZE && sizeof(TerraTri) == 16 && sizeof(TerraTurf) == TT_TURF_SIZE ? 1 : -1];
typedef char TerraSkyOffs[AT(TerraSky, grey, TS_GREY) && AT(TerraSky, pos, TS_POS) && AT(TerraSky, w, TS_W)
	&& AT(TerraSky, bw, TS_BW) && AT(TerraSky, lg, TS_LG) && AT(TerraSky, px, TS_SIZE) ? 1 : -1];
typedef char TerraMapOffs[AT(TerraMap, grey, TM_GREY) && AT(TerraMap, ypp, TM_YPP) && AT(TerraMap, rb, TM_RB)
	&& AT(TerraMap, gh, TM_GH) && AT(TerraMap, lg, TM_LG) && sizeof(TerraMap) == TM_SIZE ? 1 : -1];
typedef char TerraCurveOffs[AT(TerraCurveShade, light, TC_LIGHT) && AT(TerraCurveShade, bayer, TC_BAYER)
	&& AT(TerraCurveShade, spans, TC_SPANS) && AT(TerraCurveShade, rb, TC_RB)
	&& AT(TerraCurveShade, count, TC_COUNT) && AT(TerraCurveShade, noise, TC_NOISE)
	&& sizeof(TerraCurveShade) == TC_SIZE ? 1 : -1];
typedef char TerraEdgesOffs[AT(TerraEdges, bits, 0) && AT(TerraEdges, marks, 4)
	&& AT(TerraEdges, rb, 8) && AT(TerraEdges, w, 10) && AT(TerraEdges, vh, 12)
	&& AT(TerraEdges, horizon, 14) && AT(TerraEdges, y, 16)
	&& AT(TerraEdges, end, 18) && sizeof(TerraEdges) == 20 ? 1 : -1];

typedef char TerraMeshOffs[AT(TerraMeshProject, light, TMP_LIGHT) &&
	AT(TerraMeshProject, origin_side, TMP_ORIGIN_SIDE) && AT(TerraMeshProject, low, TMP_LOW) &&
	AT(TerraMeshProject, fx, TMP_FX) && AT(TerraMeshProject, count, TMP_COUNT) &&
	AT(TerraMeshProject, projected, TMP_PROJECTED) && sizeof(TerraMeshProject) == TMP_PROJECTED + 1 &&
	AT(MeshPoint, x, MP_X) && AT(MeshPoint, u, MP_U) && AT(MeshPoint, light, MP_LIGHT) &&
	sizeof(MeshPoint) == MP_SIZE ? 1 : -1];

#ifndef NO_ARM
static void SEC_CORE ArmTo16(const To16 *a)
{
	To16 b = *a;
	b.pal = (const uint16_t *)gPalLE;
	PceNativeCall(gArmTo16, &b);
}

static void SEC_CORE ArmTerra(uint8_t op, void *job)
{
	struct {
		UInt16 op;
		void *job;
	} a;
	a.op = op;
	a.job = job;
	PceNativeCall(gArmTerra, &a);
}
#endif

void ArmOpen(void)
{
#ifndef NO_ARM
	UInt32 cpu = 0;

	if (FtrGet(sysFtrCreator, sysFtrNumProcessorID, &cpu) != errNone || !sysFtrNumProcessorIsARM(cpu)) return;
	gArmH = DmGetResource(kArmType, kArmTo16);
	gPalLE = gArmH ? MemPtrNew(512) : NULL;
	if (gArmH && gPalLE) {
		gArmTo16 = (NativeFuncType *)MemHandleLock(gArmH);
		to16_pal_le(gPalLE, gfx_pal565());
		gfx_to16_use(ArmTo16);
	} else {
		if (gArmH) DmReleaseResource(gArmH);
		if (gPalLE) MemPtrFree(gPalLE);
		gArmH = NULL;
		gPalLE = NULL;
	}
	gTerraH = DmGetResource(kArmType, kArmTerra);
	if (gTerraH) {
		gArmTerra = (NativeFuncType *)MemHandleLock(gTerraH);
		render_terra_use(ArmTerra);
	}
#endif
}

void ArmClose(void)
{
	gfx_to16_use(NULL);
	render_terra_use(NULL);
	if (gArmTo16) MemHandleUnlock(gArmH);
	if (gArmTerra) MemHandleUnlock(gTerraH);
	if (gArmH) DmReleaseResource(gArmH);
	if (gTerraH) DmReleaseResource(gTerraH);
	if (gPalLE) MemPtrFree(gPalLE);
	gArmH = gTerraH = NULL;
	gArmTo16 = gArmTerra = NULL;
	gPalLE = NULL;
}

Boolean ArmOn(void)
{
	return gArmTo16 != NULL || gArmTerra != NULL;
}
