/* The ARM entry reads the big-endian operation, job pointer and optional
 * grass lookup pointer (terra.h). Host pointers have to be under 4 GB. */
#ifndef TERRAARM_H
#define TERRAARM_H

#include "to16arm.h"

unsigned long TO16ARM_ENTRY terraarm_entry(const void *emul, void *user, void *call68k);

#endif
