/* Native ARM code on OS 5 (src/arm/): the 16-bit conversion. */
#ifndef ARM_H
#define ARM_H

#include <PalmOS.h>
#include "section.h"

/* Loads and locks the armc resource on an ARM device and points
 * gfx_to16 at it; anywhere else, or built with -DNO_ARM, the 68k loop stays. */
void SEC_CORE ArmOpen(void);
void SEC_CORE ArmClose(void);
/* Whether the ARM code is in use, for the debug line. */
Boolean SEC_CORE ArmOn(void);

#endif
