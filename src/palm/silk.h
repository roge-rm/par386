/* Sony Clie silk area calls, written from the published Sony CLIE SDK API. */
/* Trap numbers are relative to sysLibTrapCustom. */
#ifndef SILK_H
#define SILK_H

#define silkLibName           "Sony Silk Library"
#define silkLibCreator        'SlSi'      /* a 'libr' on OS 4; built in on OS 5 */
#define sonyFtrSysInfo        1           /* feature: pointer to SonySysInfo */
#define sonyFtrVskVersion     0x0102      /* feature: Virtual Silk version */
#define sonyLibrSilk          0x00000100L /* SonySysInfo.libr: Silk library usable */

typedef struct {
	UInt16 revision;
	UInt16 rsv;
	UInt32 extn;
	UInt32 libr;
} SonySysInfo;

/* Silk library (older Clies) */
#define silkResizeNormal      0
#define silkResizeToStatus    1
#define silkResizeMax         2
extern Err SilkLibOpen(UInt16 refNum) SYS_TRAP(sysLibTrapOpen);
extern Err SilkLibClose(UInt16 refNum) SYS_TRAP(sysLibTrapClose);
extern Err SilkLibResizeDispWin(UInt16 refNum, UInt8 win) SYS_TRAP(sysLibTrapCustom + 0);
extern Err SilkLibEnableResize(UInt16 refNum) SYS_TRAP(sysLibTrapCustom + 1);
extern Err SilkLibDisableResize(UInt16 refNum) SYS_TRAP(sysLibTrapCustom + 2);

/* Virtual Silk (the same library on newer Clies, when sonyFtrVskVersion is set) */
#define vskVersionNum2        0x00020000L
#define vskStateResize        0
#define vskResizeMax          0           /* the silk area at its biggest */
#define vskResizeMin          1
#define vskResizeNone         2           /* no silk area */
#define vskStateEnable        1
#define vskResizeDisable      0
#define vskResizeVertically   1
#define vskResizeHorizontally 2
extern Err VskOpen(UInt16 refNum) SYS_TRAP(sysLibTrapOpen);
extern Err VskClose(UInt16 refNum) SYS_TRAP(sysLibTrapClose);
extern Err VskSetState(UInt16 refNum, UInt16 stateType, UInt16 state) SYS_TRAP(sysLibTrapCustom + 6);
extern Err VskGetState(UInt16 refNum, UInt16 stateType, UInt16 *stateP) SYS_TRAP(sysLibTrapCustom + 7);

#endif
