/* Declarations from Sony CLIE SDK 5.0 SonyHRLib.h (2002-07-17), as in PalmBolo. */
/* Trap offsets are relative to sysLibTrapCustom. */
#ifndef SONYHR_H
#define SONYHR_H
typedef enum { hrTinyFont = 0, hrStdFont = 8, hrFntAppFontCustomBase = 0x80 } HRFontID;
extern Err HROpen(UInt16 refNum) SYS_TRAP(sysLibTrapOpen);
extern Err HRClose(UInt16 refNum) SYS_TRAP(sysLibTrapClose);
extern Err HRGetAPIVersion(UInt16 refNum, UInt16 *versionP) SYS_TRAP(sysLibTrapCustom + 0);
extern Err HRWinScreenMode(UInt16 refNum, WinScreenModeOperation operation, UInt32 *widthP, UInt32 *heightP, UInt32 *depthP, Boolean *enableColorP) SYS_TRAP(sysLibTrapCustom + 48);
extern void HRWinCopyRectangle(UInt16 refNum, WinHandle srcWin, WinHandle dstWin, RectangleType *srcRect, Coord destX, Coord destY, WinDrawOperation mode) SYS_TRAP(sysLibTrapCustom + 2);
extern WinHandle HRWinCreateBitmapWindow(UInt16 refNum, BitmapType *bitmapP, UInt16 *error) SYS_TRAP(sysLibTrapCustom + 3);
extern WinHandle HRWinCreateOffscreenWindow(UInt16 refNum, Coord width, Coord height, WindowFormatType format, UInt16 *error) SYS_TRAP(sysLibTrapCustom + 4);
extern void HRWinDrawBitmap(UInt16 refNum, BitmapPtr bitmapP, Coord x, Coord Y) SYS_TRAP(sysLibTrapCustom + 7);
extern void HRWinDrawChars(UInt16 refNum, const Char *chars, Int16 len, Coord x, Coord y) SYS_TRAP(sysLibTrapCustom + 9);
extern void HRWinDrawLine(UInt16 refNum, Coord x1, Coord y1, Coord x2, Coord y2) SYS_TRAP(sysLibTrapCustom + 13);
extern void HRWinDrawPixel(UInt16 refNum, Coord x, Coord y) SYS_TRAP(sysLibTrapCustom + 14);
extern void HRWinDrawRectangle(UInt16 refNum, RectangleType *rP, UInt16 cornerDiam) SYS_TRAP(sysLibTrapCustom + 15);
extern void HRWinEraseLine(UInt16 refNum, Coord x1, Coord y1, Coord x2, Coord y2) SYS_TRAP(sysLibTrapCustom + 19);
extern void HRWinEraseRectangle(UInt16 refNum, RectangleType *rP, UInt16 cornerDiam) SYS_TRAP(sysLibTrapCustom + 21);
extern void HRWinGetDisplayExtent(UInt16 refNum, Coord *extentX, Coord *extentY) SYS_TRAP(sysLibTrapCustom + 26);
extern IndexedColorType HRWinGetPixel(UInt16 refNum, Coord x, Coord y) SYS_TRAP(sysLibTrapCustom + 28);
extern void HRWinInvertRectangle(UInt16 refNum, RectangleType *rP, UInt16 cornerDiam) SYS_TRAP(sysLibTrapCustom + 35);
extern void HRWinScrollRectangle(UInt16 refNum, RectangleType *rP, WinDirectionType direction, Coord distance, RectangleType *vacatedP) SYS_TRAP(sysLibTrapCustom + 49);
extern void HRWinSetClip(UInt16 refNum, RectangleType *rP) SYS_TRAP(sysLibTrapCustom + 50);
extern HRFontID HRFntSetFont(UInt16 refNum, HRFontID font) SYS_TRAP(sysLibTrapCustom + 57);
#endif
