/* The ARM entry for the 16-bit conversion (to16arm.c). user points to a
 * big endian To16 (to16.h) whose pal is in the ARM's byte order (to16_pal_le).
 * On the host the pointers in it have to be under 4 GB. */
#ifndef TO16ARM_H
#define TO16ARM_H

#ifdef __arm__
#define TO16ARM_ENTRY __attribute__((section(".text.entry")))
#else
#define TO16ARM_ENTRY
#endif

unsigned long TO16ARM_ENTRY to16arm_entry(const void *emul, void *user, void *call68k);

#endif
