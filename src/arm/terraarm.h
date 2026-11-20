/* The ARM entry for the renderer's loops (terraarm.c). user points to 6
 * bytes: the TERRA_* op, big endian, then a big endian pointer to its job
 * (terra.h), as it lies in 68k memory. On the host the pointers have to be
 * under 4 GB. */
#ifndef TERRAARM_H
#define TERRAARM_H

#include "to16arm.h"

unsigned long TO16ARM_ENTRY terraarm_entry(const void *emul, void *user, void *call68k);

#endif
