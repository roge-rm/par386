/* Palm platform code: launch handling, start/stop and the event loop. */
#include <PalmOS.h>
#include "resource.h"

#define kMinRomVersion sysMakeROMVersion(3, 1, 0, sysROMStageRelease, 0)

static Boolean MainFormHandleEvent(EventType *e)
{
	switch (e->eType) {
	case frmOpenEvent:
		FrmDrawForm(FrmGetActiveForm());
		return true;
	default:
		return false;
	}
}

static Boolean ApplicationHandleEvent(EventType *e)
{
	if (e->eType == frmLoadEvent) {
		UInt16 id = e->data.frmLoad.formID;
		FormType *form = FrmInitForm(id);
		FrmSetActiveForm(form);
		if (id == MainForm) FrmSetEventHandler(form, MainFormHandleEvent);
		return true;
	}
	return false;
}

static void AppStart(void)
{
	FrmGotoForm(MainForm);
}

static void AppStop(void)
{
	FrmCloseAllForms();
}

static void EventLoop(void)
{
	EventType e;
	UInt16 err;

	do {
		EvtGetEvent(&e, evtWaitForever);
		if (SysHandleEvent(&e)) continue;
		if (MenuHandleEvent(0, &e, &err)) continue;
		if (ApplicationHandleEvent(&e)) continue;
		FrmDispatchEvent(&e);
	} while (e.eType != appStopEvent);
}

/* Globals are only safe on a normal launch, so other launch codes do nothing here. */
UInt32 PilotMain(UInt16 cmd, void *cmdPBP, UInt16 launchFlags)
{
	UInt32 rom = 0;

	if (cmd != sysAppLaunchCmdNormalLaunch) return 0;
	FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
	if (rom < kMinRomVersion) {
		FrmAlert(RomIncompatibleAlert);
		return sysErrRomIncompatible;
	}
	AppStart();
	EventLoop();
	AppStop();
	return 0;
}
