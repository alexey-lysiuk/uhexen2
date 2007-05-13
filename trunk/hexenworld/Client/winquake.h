/*
	winquake.h
	Win32-specific Quake header file

	$Id: winquake.h,v 1.23 2007-05-13 12:05:45 sezero Exp $
*/

#if !defined(_WIN32)
#error winquake.h is only meant to be used on Windows
#endif

#ifndef __WINQUAKE_H
#define __WINQUAKE_H

#ifdef _MSC_VER
#pragma warning( disable : 4229 )  /* mgraph gets this */
#endif
#include <windows.h>
#include <winsock.h>	/* for LCC */
#include <ctype.h>

#ifndef SERVERONLY
#  ifndef CINTERFACE
#  define CINTERFACE	/* for directx macros. */
#  endif
#include <mmsystem.h>	/* for LCC */
#include <ddraw.h>
#include <dsound.h>
#endif

extern	HINSTANCE	global_hInstance;
extern	int			global_nCmdShow;

#ifndef WM_MOUSEWHEEL
#define	WM_MOUSEWHEEL		0x020A
#endif

#ifndef SERVERONLY
extern LPDIRECTDRAW		lpDD;
extern qboolean			DDActive;
extern LPDIRECTSOUND pDS;
extern LPDIRECTSOUNDBUFFER pDSBuf;

extern DWORD gSndBufSize;
//#define SNDBUFSIZE 65536
#endif

extern HWND			mainwindow;
extern qboolean		ActiveApp, Minimized;

extern qboolean		Win95, Win95old, WinNT;

void IN_RestoreOriginalMouseState (void);
void IN_SetQuakeMouseState (void);
void IN_MouseEvent (int mstate);
void IN_Accumulate (void);

extern int		window_center_x, window_center_y;
extern RECT		window_rect;

#if !defined(NO_SPLASHES)
extern HWND		hwnd_dialog;
#endif

extern HANDLE	hinput, houtput;

void IN_UpdateClipCursor (void);

void S_BlockSound (void);
void S_UnblockSound (void);

LONG CDAudio_MessageHandler (HWND, UINT, WPARAM, LPARAM);

#endif	/* __WINQUAKE_H */

