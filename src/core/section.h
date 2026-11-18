/* Code segment names. Only the Palm build uses them, the host build ignores them. */
#ifndef SECTION_H
#define SECTION_H

#ifdef __palmos__
#define SEC_CORE __attribute__((section("core")))
#else
#define SEC_CORE
#endif

#endif
