/* Calls the ARM code in the armc resource through PceNativeCall. The To16
 * job is passed as it is (big endian; src/arm/to16arm.c reads it so) with
 * pal swapped for a copy of the palette in the ARM's byte order. */
#include <PalmOS.h>
#include <stddef.h>
#include <PceNativeCall.h>
#include "arm.h"
#include "gfx.h"
#include "to16.h"

#define kArmType 'armc'
#define kArmTo16 1000

static MemHandle gArmH;
static NativeFuncType *gArmTo16;
static UInt8 *gPalLE;

/* m68k-palmos-gcc lays To16 out as to16arm.c reads it */
#define TO16_AT(f, o) (offsetof(To16, f) == (o))
typedef char To16Size[sizeof(To16) == TO16_SIZE ? 1 : -1];
typedef char To16Offs[TO16_AT(d, TO16_D) && TO16_AT(pal, TO16_PAL) && TO16_AT(dith, TO16_DITH)
	&& TO16_AT(srb, TO16_SRB) && TO16_AT(x0, TO16_X0) && TO16_AT(y1, TO16_Y1)
	&& TO16_AT(le, TO16_LE) && TO16_AT(hi, TO16_HI) ? 1 : -1];

#ifndef NO_ARM
static void SEC_CORE ArmTo16(const To16 *a)
{
	To16 b = *a;
	b.pal = (const uint16_t *)gPalLE;
	PceNativeCall(gArmTo16, &b);
}
#endif

void ArmOpen(void)
{
#ifndef NO_ARM
	UInt32 cpu = 0;

	if (FtrGet(sysFtrCreator, sysFtrNumProcessorID, &cpu) != errNone || !sysFtrNumProcessorIsARM(cpu)) return;
	gArmH = DmGetResource(kArmType, kArmTo16);
	gPalLE = MemPtrNew(512);
	if (!gArmH || !gPalLE) {
		ArmClose();
		return;
	}
	gArmTo16 = (NativeFuncType *)MemHandleLock(gArmH);
	to16_pal_le(gPalLE, gfx_pal565());
	gfx_to16_use(ArmTo16);
#endif
}

void ArmClose(void)
{
	gfx_to16_use(NULL);
	if (gArmTo16) MemHandleUnlock(gArmH);
	if (gArmH) DmReleaseResource(gArmH);
	if (gPalLE) MemPtrFree(gPalLE);
	gArmH = NULL;
	gArmTo16 = NULL;
	gPalLE = NULL;
}

Boolean ArmOn(void)
{
	return gArmTo16 != NULL;
}
