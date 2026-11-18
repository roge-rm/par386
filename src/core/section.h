/* Code segment names. Only the Palm build uses them, the host build ignores them. */
#ifndef SECTION_H
#define SECTION_H

#ifdef __palmos__
#define SEC_CORE __attribute__((section("core")))
#define SEC_UI   __attribute__((section("ui")))
#else
#define SEC_CORE
#define SEC_UI
#endif

#endif
