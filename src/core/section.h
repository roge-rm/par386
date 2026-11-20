/* Code segment names. Only the Palm build uses them, the host build ignores them. */
#ifndef SECTION_H
#define SECTION_H

#if defined(__palmos__) && defined(__m68k__)
#define SEC_CORE   __attribute__((section("core")))
#define SEC_UI     __attribute__((section("ui")))
#define SEC_RENDER __attribute__((section("render")))
#define SEC_MENU   __attribute__((section("menu")))
#define SEC_DANA   __attribute__((section("dana")))
#define SEC_CARD   __attribute__((section("cardui")))
#else
#define SEC_CORE
#define SEC_UI
#define SEC_RENDER
#define SEC_MENU
#define SEC_DANA
#define SEC_CARD
#endif

#endif
