// gl_vidnt.c -- NT GL vid component

#include "quakedef.h"
#include "quakeinc.h"
#include "resource.h"
#include "wgl_func.h"
#if !defined(NO_SPLASHES)
#include <commctrl.h>
#endif

#define WARP_WIDTH	320
#define WARP_HEIGHT	200
#define MAXWIDTH	10000
#define MAXHEIGHT	10000
#define MAX_NUMBPP	8

byte globalcolormap[VID_GRADES*256];

typedef struct {
	modestate_t	type;
	int	width;
	int	height;
	int	modenum;
	int	dib;
	int	fullscreen;
	int	bpp;
	int	halfscreen;
	char	modedesc[33];
} vmode_t;

typedef struct {
	int	width;
	int	height;
} stdmode_t;

#define RES_640X480	3
static const stdmode_t	std_modes[] = {
	{320, 240},	// 0
	{400, 300},	// 1
	{512, 384},	// 2
	{640, 480},	// 3 == RES_640X480, this is our default, below
			//		this is the lowresmodes region.
			//		either do not change its order,
			//		or change the above define, too
	{800,  600},	// 4, RES_640X480 + 1
	{1024, 768},	// 5, RES_640X480 + 2
	{1280, 1024},	// 6
	{1600, 1200}	// 7
};

#ifdef GL_DLSYM
HINSTANCE	hInstGL;
static const char	*gl_library;
#endif
const char	*gl_vendor;
const char	*gl_renderer;
const char	*gl_version;
const char	*gl_extensions;
int		gl_max_size = 256;
qboolean	is8bit = false;
qboolean	is_3dfx = false;
qboolean	gl_mtexable = false;
static int	num_tmus = 1;

qboolean	DDActive;
qboolean	scr_skipupdate;

#define MAX_MODE_LIST	40
#define MAX_STDMODES	(sizeof(std_modes) / sizeof(std_modes[0]))
#define NUM_LOWRESMODES	(RES_640X480)
static vmode_t	fmodelist[MAX_MODE_LIST+1];	// list of enumerated fullscreen modes
static vmode_t	wmodelist[MAX_STDMODES +1];	// list of standart 4:3 windowed modes
static vmode_t	*modelist;	// modelist in use, points to one of the above lists
static int	num_fmodes;
static int	num_wmodes;
static int	*nummodes;
static vmode_t	badmode;

extern qboolean	draw_reinit;
static DEVMODE	gdevmode;
static qboolean	vid_initialized = false;
static qboolean	windowed;
static qboolean vid_canalttab = false;
static qboolean vid_wassuspended = false;
static int	enable_mouse;
static HICON	hIcon;

static int	DIBWidth, DIBHeight;
static RECT	WindowRect;
static DWORD	WindowStyle, ExWindowStyle;

HWND		mainwindow, dibwindow;

static int	vid_modenum = NO_MODE;
static int	vid_default = MODE_WINDOWED;
static int	vid_deskwidth, vid_deskheight, vid_deskbpp, vid_deskmode;
static int	bpplist[MAX_NUMBPP][2];
static int	windowed_default;
unsigned char	vid_curpal[256*3];
static qboolean fullsbardraw = false;

static HGLRC	baseRC;
static HDC	maindc;

cvar_t		gl_ztrick = {"gl_ztrick","0",true};
cvar_t		gl_purge_maptex = {"gl_purge_maptex", "1", true};
		/* whether or not map-specific OGL textures
		   are flushed from map. default == yes  */

viddef_t	vid;				// global video state

float		RTint[256],GTint[256],BTint[256];
unsigned short	d_8to16table[256];
unsigned	d_8to24table[256];
//unsigned	d_8to24table3dfx[256];
unsigned	d_8to24TranslucentTable[256];
#ifdef	OLD_8_BIT_PALETTE_CODE
unsigned char	inverse_pal[(1<<INVERSE_PAL_TOTAL_BITS)+1]; // +1: COM_LoadStackFile puts a 0 at the end of the data
#else
unsigned char	d_15to8table[65536];
#endif

static PIXELFORMATDESCRIPTOR pfd = {
	sizeof(PIXELFORMATDESCRIPTOR),	// size of this pfd
	1,				// version number
	PFD_DRAW_TO_WINDOW 		// support window
	|  PFD_SUPPORT_OPENGL 	// support OpenGL
	|  PFD_DOUBLEBUFFER ,	// double buffered
	PFD_TYPE_RGBA,			// RGBA type
	24,				// 24-bit color depth
	0, 0, 0, 0, 0, 0,		// color bits ignored
	0,				// no alpha buffer
	0,				// shift bit ignored
	0,				// no accumulation buffer
	0, 0, 0, 0, 			// accum bits ignored
	24,				// 24-bit z-buffer
	8,				// 8-bit stencil buffer
	0,				// no auxiliary buffer
	PFD_MAIN_PLANE,			// main layer
	0,				// reserved
	0, 0, 0				// layer masks ignored
};

float		gldepthmin, gldepthmax;
extern int	lightmap_textures;
extern int	lightmap_bytes;	// in gl_rsurf.c

modestate_t	modestate = MS_UNINIT;

void VID_MenuDraw (void);
void VID_MenuKey (int key);

LONG WINAPI MainWndProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static void AppActivate(BOOL fActive, BOOL minimize);
static char *VID_GetModeDescription (int mode);
static void ClearAllStates (void);
static void VID_UpdateWindowStatus (void);
static void GL_Init (void);

typedef void (APIENTRY *FX_SET_PALETTE_EXT)(int, int, int, int, int, const void*);
static FX_SET_PALETTE_EXT MyglColorTableEXT;
typedef BOOL (APIENTRY *GAMMA_RAMP_FN)(HDC, LPVOID);
static GAMMA_RAMP_FN GetDeviceGammaRamp_f;
static GAMMA_RAMP_FN SetDeviceGammaRamp_f;

//====================================

// Note that 0 is MODE_WINDOWED
// Note that 3 is MODE_FULLSCREEN_DEFAULT
cvar_t		vid_mode = {"vid_mode","0", false};
cvar_t		_enable_mouse = {"_enable_mouse","0", true};

int		window_center_x, window_center_y, window_x, window_y, window_width, window_height;
RECT		window_rect;

qboolean	have_stencil = false;

extern unsigned short	ramps[3][256];
static unsigned short	orig_ramps[3][256];
static qboolean	gammaworks = false;
qboolean	gl_dogamma = false;

extern void	D_ClearOpenGLTextures(int); 
extern void	R_InitParticleTexture(void); 
extern void	Mod_ReloadTextures (void);

//====================================

// for compatability with software renderer

void VID_LockBuffer (void)
{
}

void VID_UnlockBuffer (void)
{
}

void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
}

void D_EndDirectRect (int x, int y, int width, int height)
{
}

void VID_HandlePause (qboolean paused)
{
}


//====================================

static void CenterWindow(HWND hWndCenter, int width, int height, BOOL lefttopjustify)
{
	int	CenterX, CenterY;

	CenterX = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
	CenterY = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
	if (CenterX > CenterY*2)
		CenterX >>= 1;	// dual screens
	CenterX = (CenterX < 0) ? 0: CenterX;
	CenterY = (CenterY < 0) ? 0: CenterY;
	SetWindowPos (hWndCenter, NULL, CenterX, CenterY, 0, 0,
			SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_DRAWFRAME);
}

static void VID_ConWidth(int modenum)
{
	int i;

	vid.width  = vid.conwidth  = modelist[modenum].width;
	vid.height = vid.conheight = modelist[modenum].height;

	// This will display a bigger hud and readable fonts at high
	// resolutions. The fonts will be somewhat distorted, though
	i = COM_CheckParm("-conwidth");
	if (i != 0 && i < com_argc-1) {
		vid.conwidth = atoi(com_argv[i+1]);
		vid.conwidth &= 0xfff8; // make it a multiple of eight
		if (vid.conwidth < 320)
			vid.conwidth = 320;
		// pick a conheight that matches with correct aspect
		vid.conheight = vid.conwidth*3 / 4;
		i = COM_CheckParm("-conheight");
		if (i != 0 && i < com_argc-1)
			vid.conheight = atoi(com_argv[i+1]);
		if (vid.conheight < 200)
			vid.conheight = 200;
		if (vid.conwidth > modelist[modenum].width)
			vid.conwidth = modelist[modenum].width;
		if (vid.conheight > modelist[modenum].height)
			vid.conheight = modelist[modenum].height;

		vid.width = vid.conwidth;
		vid.height = vid.conheight;
	}
}

static qboolean VID_SetWindowedMode (int modenum)
{
	HDC	hdc;
	int	lastmodestate, width, height;
	RECT	rect;

	lastmodestate = modestate;

	// Pa3PyX: set the original fullscreen mode if
	// we are switching to window from fullscreen.
	if (lastmodestate == MS_FULLDIB)
		ChangeDisplaySettings(NULL, 0);

	WindowRect.top = WindowRect.left = 0;

	WindowRect.right = modelist[modenum].width;
	WindowRect.bottom = modelist[modenum].height;

	DIBWidth = modelist[modenum].width;
	DIBHeight = modelist[modenum].height;

	WindowStyle = WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_SYSMENU |
				  WS_MINIMIZEBOX;
	ExWindowStyle = 0;

	rect = WindowRect;
	AdjustWindowRectEx(&rect, WindowStyle, FALSE, 0);

	width = rect.right - rect.left;
	height = rect.bottom - rect.top;

	// Create the DIB window
	dibwindow = CreateWindowEx (
		 ExWindowStyle,
#ifndef H2W
		 "HexenII",
		 "HexenII",
#else
		 "HexenWorld",
		 "HexenWorld",
#endif
		 WindowStyle,
		 rect.left, rect.top,
		 width,
		 height,
		 NULL,
		 NULL,
		 global_hInstance,
		 NULL);

	if (!dibwindow)
		Sys_Error ("Couldn't create DIB window");

	// Center and show the DIB window
	CenterWindow(dibwindow, WindowRect.right - WindowRect.left,
				 WindowRect.bottom - WindowRect.top, false);

	ShowWindow (dibwindow, SW_SHOWDEFAULT);
	UpdateWindow (dibwindow);

	modestate = MS_WINDOWED;

	// Because we have set the background brush for the window to NULL
	// (to avoid flickering when re-sizing the window on the desktop),
	// we clear the window to black when created, otherwise it will be
	// empty while Quake starts up.
	hdc = GetDC(dibwindow);
	PatBlt(hdc,0,0,WindowRect.right,WindowRect.bottom,BLACKNESS);
	ReleaseDC(dibwindow, hdc);

	VID_ConWidth(modenum);

	vid.numpages = 2;

	mainwindow = dibwindow;

	SendMessage (mainwindow, WM_SETICON, (WPARAM)TRUE, (LPARAM)hIcon);
	SendMessage (mainwindow, WM_SETICON, (WPARAM)FALSE, (LPARAM)hIcon);

	return true;
}


static qboolean VID_SetFullDIBMode (int modenum)
{
	HDC	hdc;
	int	lastmodestate, width, height;
	RECT	rect;

	lastmodestate = modestate;

	pfd.cColorBits = modelist[modenum].bpp;

	gdevmode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
	if (!Win95old)
	{
		gdevmode.dmFields |= DM_BITSPERPEL;
		gdevmode.dmBitsPerPel = modelist[modenum].bpp;
	}
	gdevmode.dmPelsWidth = modelist[modenum].width << modelist[modenum].halfscreen;
	gdevmode.dmPelsHeight = modelist[modenum].height;
	gdevmode.dmSize = sizeof (gdevmode);

	if (ChangeDisplaySettings (&gdevmode, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
		Sys_Error ("Couldn't set fullscreen DIB mode");

	WindowRect.top = WindowRect.left = 0;

	WindowRect.right = modelist[modenum].width;
	WindowRect.bottom = modelist[modenum].height;

	DIBWidth = modelist[modenum].width;
	DIBHeight = modelist[modenum].height;

	WindowStyle = WS_POPUP;
	ExWindowStyle = 0;

	rect = WindowRect;
	AdjustWindowRectEx(&rect, WindowStyle, FALSE, 0);

	width = rect.right - rect.left;
	height = rect.bottom - rect.top;

	// Create the DIB window
	dibwindow = CreateWindowEx (
		 ExWindowStyle,
#ifndef H2W
		 "HexenII",
		 "HexenII",
#else
		 "HexenWorld",
		 "HexenWorld",
#endif
		 WindowStyle,
		 rect.left, rect.top,
		 width,
		 height,
		 NULL,
		 NULL,
		 global_hInstance,
		 NULL);

	if (!dibwindow)
		Sys_Error ("Couldn't create DIB window");

	ShowWindow (dibwindow, SW_SHOWDEFAULT);
	UpdateWindow (dibwindow);

	modestate = MS_FULLDIB;

	// Because we have set the background brush for the window to NULL
	// (to avoid flickering when re-sizing the window on the desktop),
	// we clear the window to black when created, otherwise it will be
	// empty while Quake starts up.
	hdc = GetDC(dibwindow);
	PatBlt(hdc,0,0,WindowRect.right,WindowRect.bottom,BLACKNESS);
	ReleaseDC(dibwindow, hdc);

	VID_ConWidth(modenum);

	vid.numpages = 2;

// needed because we're not getting WM_MOVE messages fullscreen on NT
	window_x = 0;
	window_y = 0;

	mainwindow = dibwindow;

	SendMessage (mainwindow, WM_SETICON, (WPARAM)TRUE, (LPARAM)hIcon);
	SendMessage (mainwindow, WM_SETICON, (WPARAM)FALSE, (LPARAM)hIcon);

	return true;
}


static int VID_SetMode (int modenum, unsigned char *palette)
{
	int		original_mode;
	qboolean	stat = false;
	MSG		msg;

	if (modenum < 0 || modenum >= *nummodes)
		Sys_Error ("Bad video mode\n");

	CDAudio_Pause ();

	if (vid_modenum == NO_MODE)
		original_mode = windowed_default;
	else
		original_mode = vid_modenum;

	// Set either the fullscreen or windowed mode
	if (modelist[modenum].type == MS_WINDOWED)
	{
		if (_enable_mouse.value)
		{
			stat = VID_SetWindowedMode(modenum);
			IN_ActivateMouse ();
			IN_HideMouse ();
		}
		else
		{
			stat = VID_SetWindowedMode(modenum);
			IN_DeactivateMouse ();
			IN_ShowMouse ();
		}
	}
	else if (modelist[modenum].type == MS_FULLDIB)
	{
		stat = VID_SetFullDIBMode(modenum);
		IN_ActivateMouse ();
		IN_HideMouse ();
	}
	else
	{
		Sys_Error ("VID_SetMode: Bad mode type in modelist");
	}

	if (!stat)
	{
		Sys_Error ("Couldn't set video mode");
	}

	window_width = DIBWidth;
	window_height = DIBHeight;
	VID_UpdateWindowStatus ();

	CDAudio_Resume ();

// now we try to make sure we get the focus on the mode switch, because
// sometimes in some systems we don't.  We grab the foreground, then
// finish setting up, pump all our messages, and sleep for a little while
// to let messages finish bouncing around the system, then we put
// ourselves at the top of the z order, then grab the foreground again,
// Who knows if it helps, but it probably doesn't hurt
	SetForegroundWindow (mainwindow);
	//VID_SetPalette (palette);
	vid_modenum = modenum;

	while (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))
	{
		TranslateMessage (&msg);
		DispatchMessage (&msg);
	}

	Sleep (100);

	SetWindowPos (mainwindow, HWND_TOP, 0, 0, 0, 0,
				  SWP_DRAWFRAME | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW |
				  SWP_NOCOPYBITS);

	SetForegroundWindow (mainwindow);

// fix the leftover Alt from any Alt-Tab or the like that switched us away
	ClearAllStates ();

	//VID_SetPalette (palette);
	//vid.recalc_refdef = 1;

	return true;
}


/*
================
VID_UpdateWindowStatus
================
*/
static void VID_UpdateWindowStatus (void)
{
	window_rect.left = window_x;
	window_rect.top = window_y;
	window_rect.right = window_x + window_width;
	window_rect.bottom = window_y + window_height;
	window_center_x = (window_rect.left + window_rect.right) / 2;
	window_center_y = (window_rect.top + window_rect.bottom) / 2;

	IN_UpdateClipCursor ();
}


//====================================

static void VID_Check3dfxGamma (void)
{
	if (!strstr(gl_extensions, "WGL_3DFX_gamma_control"))
	{
		GetDeviceGammaRamp_f = GetDeviceGammaRamp;
		SetDeviceGammaRamp_f = SetDeviceGammaRamp;
	}
	else
	{
		GetDeviceGammaRamp_f = wglGetProcAddress_fp("wglGetDeviceGammaRamp3DFX");
		SetDeviceGammaRamp_f = wglGetProcAddress_fp("wglSetDeviceGammaRamp3DFX");
		if (GetDeviceGammaRamp_f && SetDeviceGammaRamp_f)
			Con_Printf("Using 3Dfx specific gamma control\n");
		else {
			GetDeviceGammaRamp_f = GetDeviceGammaRamp;
			SetDeviceGammaRamp_f = SetDeviceGammaRamp;
		}
	}
}

static void VID_InitGamma (void)
{
	GetDeviceGammaRamp_f = NULL;
	SetDeviceGammaRamp_f = NULL;

	VID_Check3dfxGamma ();

	if (GetDeviceGammaRamp_f)
		gammaworks = GetDeviceGammaRamp_f(maindc, orig_ramps);
	else
		gammaworks = false;

	if (!gammaworks)
	{
		// we can still adjust the brightness...
		Con_Printf("gamma not available, using gl tricks\n");
		gl_dogamma = true;
	}
}


int		texture_extension_number = 1;

static void CheckMultiTextureExtensions(void)
{
	gl_mtexable = false;

	if (COM_CheckParm("-nomtex"))
	{
		Con_Printf("Multitexture extensions disabled\n");
	}
	else if (strstr(gl_extensions, "GL_ARB_multitexture"))
	{
		Con_Printf("ARB Multitexture extensions found\n");

		glGetIntegerv_fp(GL_MAX_TEXTURE_UNITS_ARB, &num_tmus);
		if (num_tmus < 2) {
			Con_Printf("not enough TMUs, ignoring multitexture\n");
			return;
		}

		glMultiTexCoord2fARB_fp = (void *) wglGetProcAddress_fp("glMultiTexCoord2fARB");
		glActiveTextureARB_fp = (void *) wglGetProcAddress_fp("glActiveTextureARB");
		if ((glMultiTexCoord2fARB_fp == NULL) ||
		    (glActiveTextureARB_fp == NULL)) {
			Con_Printf ("Couldn't link to multitexture functions\n");
			return;
		}

		Con_Printf("Found %i TMUs support\n", num_tmus);
		gl_mtexable = true;

		// start up with the correct texture selected!
		glDisable_fp(GL_TEXTURE_2D);
		glActiveTextureARB_fp(GL_TEXTURE0_ARB);
	}
	else
	{
		Con_Printf("GL_ARB_multitexture not found\n");
	}
}

static void CheckStencilBuffer(void)
{
	have_stencil = false;

#ifdef GL_DLSYM
	glStencilFunc_fp = (glStencilFunc_f) GetProcAddress(hInstGL, "glStencilFunc");
	glStencilOp_fp = (glStencilOp_f) GetProcAddress(hInstGL, "glStencilOp");
	glClearStencil_fp = (glClearStencil_f) GetProcAddress(hInstGL, "glClearStencil");
	if ((glStencilFunc_fp == NULL) ||
	    (glStencilOp_fp == NULL)   ||
	    (glClearStencil_fp == NULL))
	{
		Con_Printf ("glStencil functions not available\n");
		return;
	}
#endif

	if (pfd.cStencilBits)
	{
		Con_Printf("Stencil buffer created with %d bits\n", pfd.cStencilBits);
		have_stencil = true;
	}
}

static void GL_InitLightmapBits (void)
{
	gl_lightmap_format = GL_LUMINANCE;
	if (COM_CheckParm ("-lm_1"))
		gl_lightmap_format = GL_LUMINANCE;
	else if (COM_CheckParm ("-lm_a"))
		gl_lightmap_format = GL_ALPHA;
	else if (COM_CheckParm ("-lm_i"))
		gl_lightmap_format = GL_INTENSITY;
//	else if (COM_CheckParm ("-lm_2"))
//		gl_lightmap_format = GL_RGBA4;
	else if (COM_CheckParm ("-lm_4"))
		gl_lightmap_format = GL_RGBA;

	switch (gl_lightmap_format)
	{
	case GL_RGBA:
		lightmap_bytes = 4;
		break;
//	case GL_RGBA4:
//		lightmap_bytes = 2;
//		break;
	case GL_LUMINANCE:
	case GL_INTENSITY:
	case GL_ALPHA:
		lightmap_bytes = 1;
		break;
	}
}

#ifdef GL_DLSYM
static void GL_CloseLibrary(void)
{
	// clear the wgl function pointers
	wglGetProcAddress_fp = NULL;
	wglCreateContext_fp = NULL;
	wglDeleteContext_fp = NULL;
	wglMakeCurrent_fp = NULL;
	wglGetCurrentContext_fp = NULL;
	wglGetCurrentDC_fp = NULL;

	// free the library
	if (hInstGL != NULL)
		FreeLibrary(hInstGL);
	hInstGL = NULL;
}

static qboolean GL_OpenLibrary(const char *name)
{
	Con_Printf("Loading OpenGL library %s\n", name);

	// open the library
	if (!(hInstGL = LoadLibrary(name)))
	{
		Con_Printf("Unable to LoadLibrary %s\n", name);
		return false;
	}

	// link to necessary wgl functions
	wglGetProcAddress_fp = (void *)GetProcAddress(hInstGL, "wglGetProcAddress");
	if (wglGetProcAddress_fp == NULL) {Sys_Error("Couldn't link to wglGetProcAddress");}
	wglCreateContext_fp = (void *)GetProcAddress(hInstGL, "wglCreateContext");
	if (wglCreateContext_fp == NULL) {Sys_Error("Couldn't link to wglCreateContext");}
	wglDeleteContext_fp = (void *)GetProcAddress(hInstGL, "wglDeleteContext");
	if (wglDeleteContext_fp == NULL) {Sys_Error("Couldn't link to wglDeleteContext");}
	wglMakeCurrent_fp = (void *)GetProcAddress(hInstGL, "wglMakeCurrent");
	if (wglMakeCurrent_fp == NULL) {Sys_Error("Couldn't link to wglMakeCurrent");}
	wglGetCurrentContext_fp = (void *)GetProcAddress(hInstGL, "wglGetCurrentContext");
	if (wglGetCurrentContext_fp == NULL) {Sys_Error("Couldn't link to wglGetCurrentContext");}
	wglGetCurrentDC_fp = (void *)GetProcAddress(hInstGL, "wglGetCurrentDC");
	if (wglGetCurrentDC_fp == NULL) {Sys_Error("Couldn't link to wglGetCurrentDC");}

	return true;
}

static void GL_Init_Functions(void)
{
  glBegin_fp = (glBegin_f) GetProcAddress(hInstGL, "glBegin");
  if (glBegin_fp == 0) {Sys_Error("glBegin not found in GL library");}
  glEnd_fp = (glEnd_f) GetProcAddress(hInstGL, "glEnd");
  if (glEnd_fp == 0) {Sys_Error("glEnd not found in GL library");}
  glEnable_fp = (glEnable_f) GetProcAddress(hInstGL, "glEnable");
  if (glEnable_fp == 0) {Sys_Error("glEnable not found in GL library");}
  glDisable_fp = (glDisable_f) GetProcAddress(hInstGL, "glDisable");
  if (glDisable_fp == 0) {Sys_Error("glDisable not found in GL library");}
#ifdef H2W
  glIsEnabled_fp = (glIsEnabled_f) GetProcAddress(hInstGL, "glIsEnabled");
  if (glIsEnabled_fp == 0) {Sys_Error("glIsEnabled not found in GL library");}
#endif
  glFinish_fp = (glFinish_f) GetProcAddress(hInstGL, "glFinish");
  if (glFinish_fp == 0) {Sys_Error("glFinish not found in GL library");}
  glFlush_fp = (glFlush_f) GetProcAddress(hInstGL, "glFlush");
  if (glFlush_fp == 0) {Sys_Error("glFlush not found in GL library");}
  glClear_fp = (glClear_f) GetProcAddress(hInstGL, "glClear");
  if (glClear_fp == 0) {Sys_Error("glClear not found in GL library");}

  glOrtho_fp = (glOrtho_f) GetProcAddress(hInstGL, "glOrtho");
  if (glOrtho_fp == 0) {Sys_Error("glOrtho not found in GL library");}
  glFrustum_fp = (glFrustum_f) GetProcAddress(hInstGL, "glFrustum");
  if (glFrustum_fp == 0) {Sys_Error("glFrustum not found in GL library");}
  glViewport_fp = (glViewport_f) GetProcAddress(hInstGL, "glViewport");
  if (glViewport_fp == 0) {Sys_Error("glViewport not found in GL library");}
  glPushMatrix_fp = (glPushMatrix_f) GetProcAddress(hInstGL, "glPushMatrix");
  if (glPushMatrix_fp == 0) {Sys_Error("glPushMatrix not found in GL library");}
  glPopMatrix_fp = (glPopMatrix_f) GetProcAddress(hInstGL, "glPopMatrix");
  if (glPopMatrix_fp == 0) {Sys_Error("glPopMatrix not found in GL library");}
  glLoadIdentity_fp = (glLoadIdentity_f) GetProcAddress(hInstGL, "glLoadIdentity");
  if (glLoadIdentity_fp == 0) {Sys_Error("glLoadIdentity not found in GL library");}
  glMatrixMode_fp = (glMatrixMode_f) GetProcAddress(hInstGL, "glMatrixMode");
  if (glMatrixMode_fp == 0) {Sys_Error("glMatrixMode not found in GL library");}
  glLoadMatrixf_fp = (glLoadMatrixf_f) GetProcAddress(hInstGL, "glLoadMatrixf");
  if (glLoadMatrixf_fp == 0) {Sys_Error("glLoadMatrixf not found in GL library");}

  glVertex2f_fp = (glVertex2f_f) GetProcAddress(hInstGL, "glVertex2f");
  if (glVertex2f_fp == 0) {Sys_Error("glVertex2f not found in GL library");}
  glVertex3f_fp = (glVertex3f_f) GetProcAddress(hInstGL, "glVertex3f");
  if (glVertex3f_fp == 0) {Sys_Error("glVertex3f not found in GL library");}
  glVertex3fv_fp = (glVertex3fv_f) GetProcAddress(hInstGL, "glVertex3fv");
  if (glVertex3fv_fp == 0) {Sys_Error("glVertex3fv not found in GL library");}
  glTexCoord2f_fp = (glTexCoord2f_f) GetProcAddress(hInstGL, "glTexCoord2f");
  if (glTexCoord2f_fp == 0) {Sys_Error("glTexCoord2f not found in GL library");}
  glTexCoord3f_fp = (glTexCoord3f_f) GetProcAddress(hInstGL, "glTexCoord3f");
  if (glTexCoord3f_fp == 0) {Sys_Error("glTexCoord3f not found in GL library");}
  glColor4f_fp = (glColor4f_f) GetProcAddress(hInstGL, "glColor4f");
  if (glColor4f_fp == 0) {Sys_Error("glColor4f not found in GL library");}
  glColor4fv_fp = (glColor4fv_f) GetProcAddress(hInstGL, "glColor4fv");
  if (glColor4fv_fp == 0) {Sys_Error("glColor4fv not found in GL library");}
#ifdef H2W
  glColor4ub_fp = (glColor4ub_f) GetProcAddress(hInstGL, "glColor4ub");
  if (glColor4ub_fp == 0) {Sys_Error("glColor4ub not found in GL library");}
#endif
  glColor4ubv_fp = (glColor4ubv_f) GetProcAddress(hInstGL, "glColor4ubv");
  if (glColor4ubv_fp == 0) {Sys_Error("glColor4ubv not found in GL library");}
  glColor3f_fp = (glColor3f_f) GetProcAddress(hInstGL, "glColor3f");
  if (glColor3f_fp == 0) {Sys_Error("glColor3f not found in GL library");}
  glColor3ubv_fp = (glColor3ubv_f) GetProcAddress(hInstGL, "glColor3ubv");
  if (glColor3ubv_fp == 0) {Sys_Error("glColor3ubv not found in GL library");}
  glClearColor_fp = (glClearColor_f) GetProcAddress(hInstGL, "glClearColor");
  if (glClearColor_fp == 0) {Sys_Error("glClearColor not found in GL library");}

  glRotatef_fp = (glRotatef_f) GetProcAddress(hInstGL, "glRotatef");
  if (glRotatef_fp == 0) {Sys_Error("glRotatef not found in GL library");}
  glTranslatef_fp = (glTranslatef_f) GetProcAddress(hInstGL, "glTranslatef");
  if (glTranslatef_fp == 0) {Sys_Error("glTranslatef not found in GL library");}

  glBindTexture_fp = (glBindTexture_f) GetProcAddress(hInstGL, "glBindTexture");
  if (glBindTexture_fp == 0) {Sys_Error("glBindTexture not found in GL library");}
  glDeleteTextures_fp = (glDeleteTextures_f) GetProcAddress(hInstGL, "glDeleteTextures");
  if (glDeleteTextures_fp == 0) {Sys_Error("glDeleteTextures not found in GL library");}
  glTexParameterf_fp = (glTexParameterf_f) GetProcAddress(hInstGL, "glTexParameterf");
  if (glTexParameterf_fp == 0) {Sys_Error("glTexParameterf not found in GL library");}
  glTexEnvf_fp = (glTexEnvf_f) GetProcAddress(hInstGL, "glTexEnvf");
  if (glTexEnvf_fp == 0) {Sys_Error("glTexEnvf not found in GL library");}
  glScalef_fp = (glScalef_f) GetProcAddress(hInstGL, "glScalef");
  if (glScalef_fp == 0) {Sys_Error("glScalef not found in GL library");}
  glTexImage2D_fp = (glTexImage2D_f) GetProcAddress(hInstGL, "glTexImage2D");
  if (glTexImage2D_fp == 0) {Sys_Error("glTexImage2D not found in GL library");}
#ifdef H2W
  glTexSubImage2D_fp = (glTexSubImage2D_f) GetProcAddress(hInstGL, "glTexSubImage2D");
  if (glTexSubImage2D_fp == 0) {Sys_Error("glTexSubImage2D not found in GL library");}
#endif

  glAlphaFunc_fp = (glAlphaFunc_f) GetProcAddress(hInstGL, "glAlphaFunc");
  if (glAlphaFunc_fp == 0) {Sys_Error("glAlphaFunc not found in GL library");}
  glBlendFunc_fp = (glBlendFunc_f) GetProcAddress(hInstGL, "glBlendFunc");
  if (glBlendFunc_fp == 0) {Sys_Error("glBlendFunc not found in GL library");}
  glShadeModel_fp = (glShadeModel_f) GetProcAddress(hInstGL, "glShadeModel");
  if (glShadeModel_fp == 0) {Sys_Error("glShadeModel not found in GL library");}
  glPolygonMode_fp = (glPolygonMode_f) GetProcAddress(hInstGL, "glPolygonMode");
  if (glPolygonMode_fp == 0) {Sys_Error("glPolygonMode not found in GL library");}
  glDepthMask_fp = (glDepthMask_f) GetProcAddress(hInstGL, "glDepthMask");
  if (glDepthMask_fp == 0) {Sys_Error("glDepthMask not found in GL library");}
  glDepthRange_fp = (glDepthRange_f) GetProcAddress(hInstGL, "glDepthRange");
  if (glDepthRange_fp == 0) {Sys_Error("glDepthRange not found in GL library");}
  glDepthFunc_fp = (glDepthFunc_f) GetProcAddress(hInstGL, "glDepthFunc");
  if (glDepthFunc_fp == 0) {Sys_Error("glDepthFunc not found in GL library");}

  glDrawBuffer_fp = (glDrawBuffer_f) GetProcAddress(hInstGL, "glDrawBuffer");
  if (glDrawBuffer_fp == 0) {Sys_Error("glDrawBuffer not found in GL library");}
  glReadBuffer_fp = (glDrawBuffer_f) GetProcAddress(hInstGL, "glReadBuffer");
  if (glReadBuffer_fp == 0) {Sys_Error("glReadBuffer not found in GL library");}
  glReadPixels_fp = (glReadPixels_f) GetProcAddress(hInstGL, "glReadPixels");
  if (glReadPixels_fp == 0) {Sys_Error("glReadPixels not found in GL library");}
  glHint_fp = (glHint_f) GetProcAddress(hInstGL, "glHint");
  if (glHint_fp == 0) {Sys_Error("glHint not found in GL library");}
  glCullFace_fp = (glCullFace_f) GetProcAddress(hInstGL, "glCullFace");
  if (glCullFace_fp == 0) {Sys_Error("glCullFace not found in GL library");}

  glGetIntegerv_fp = (glGetIntegerv_f) GetProcAddress(hInstGL, "glGetIntegerv");
  if (glGetIntegerv_fp == 0) {Sys_Error("glGetIntegerv not found in GL library");}

  glGetString_fp = (glGetString_f) GetProcAddress(hInstGL, "glGetString");
  if (glGetString_fp == 0) {Sys_Error("glGetString not found in GL library");}
  glGetFloatv_fp = (glGetFloatv_f) GetProcAddress(hInstGL, "glGetFloatv");
  if (glGetFloatv_fp == 0) {Sys_Error("glGetFloatv not found in GL library");}
}

static void GL_ResetFunctions(void)
{
  glBegin_fp = NULL;
  glEnd_fp = NULL;
  glEnable_fp = NULL;
  glDisable_fp = NULL;
  glIsEnabled_fp = NULL;
  glFinish_fp = NULL;
  glFlush_fp = NULL;
  glClear_fp = NULL;

  glOrtho_fp = NULL;
  glFrustum_fp = NULL;
  glViewport_fp = NULL;
  glPushMatrix_fp = NULL;
  glPopMatrix_fp = NULL;
  glLoadIdentity_fp = NULL;
  glMatrixMode_fp = NULL;
  glLoadMatrixf_fp = NULL;

  glVertex2f_fp = NULL;
  glVertex3f_fp = NULL;
  glVertex3fv_fp = NULL;
  glTexCoord2f_fp = NULL;
  glTexCoord3f_fp = NULL;
  glColor4f_fp = NULL;
  glColor4fv_fp = NULL;
  glColor4ub_fp = NULL;
  glColor4ubv_fp = NULL;
  glColor3f_fp = NULL;
  glColor3ubv_fp = NULL;
  glClearColor_fp = NULL;

  glRotatef_fp = NULL;
  glTranslatef_fp = NULL;

  glBindTexture_fp = NULL;
  glDeleteTextures_fp = NULL;
  glTexParameterf_fp = NULL;
  glTexEnvf_fp = NULL;
  glScalef_fp = NULL;
  glTexImage2D_fp = NULL;
  glTexSubImage2D_fp = NULL;

  glAlphaFunc_fp = NULL;
  glBlendFunc_fp = NULL;
  glShadeModel_fp = NULL;
  glPolygonMode_fp = NULL;
  glDepthMask_fp = NULL;
  glDepthRange_fp = NULL;
  glDepthFunc_fp = NULL;

  glDrawBuffer_fp = NULL;
  glReadBuffer_fp = NULL;
  glReadPixels_fp = NULL;
  glHint_fp = NULL;
  glCullFace_fp = NULL;

  glGetIntegerv_fp = NULL;

  glGetString_fp = NULL;
  glGetFloatv_fp = NULL;

  have_stencil = false;
  glStencilFunc_fp = NULL;
  glStencilOp_fp = NULL;
  glClearStencil_fp = NULL;

  gl_mtexable = false;
  glActiveTextureARB_fp = NULL;
  glMultiTexCoord2fARB_fp = NULL;

  is8bit = false;
  MyglColorTableEXT = NULL;

  GetDeviceGammaRamp_f = NULL;
  SetDeviceGammaRamp_f = NULL;
}
#else	// GL_DLSYM
static void GL_ResetFunctions(void)
{
  gl_mtexable = false;
  glActiveTextureARB_fp = NULL;
  glMultiTexCoord2fARB_fp = NULL;

  is8bit = false;
  MyglColorTableEXT = NULL;

  GetDeviceGammaRamp_f = NULL;
  SetDeviceGammaRamp_f = NULL;
}
#endif	// GL_DLSYM

/*
===============
GL_Init
===============
*/
static void GL_Init (void)
{
	PIXELFORMATDESCRIPTOR	new_pfd;

	Con_Printf ("Video mode %s initialized\n", VID_GetModeDescription (vid_modenum));
	if (!DescribePixelFormat(maindc, GetPixelFormat(maindc), sizeof(PIXELFORMATDESCRIPTOR), &new_pfd))
		Sys_Error ("DescribePixelFormat failed\n");
	Con_Printf("Pixel format: c: %d, z: %d, s: %d\n",
		new_pfd.cColorBits, new_pfd.cDepthBits, new_pfd.cStencilBits);
	if (new_pfd.dwFlags & PFD_GENERIC_FORMAT)
		Con_Printf ("WARNING: Hardware acceleration not present\n");
	else if (new_pfd.dwFlags & PFD_GENERIC_ACCELERATED)
		Con_Printf ("Found MCD acceleration\n");
	

#ifdef GL_DLSYM
	// initialize gl function pointers
	GL_Init_Functions();
#endif
	gl_vendor = glGetString_fp (GL_VENDOR);
	Con_Printf ("GL_VENDOR: %s\n", gl_vendor);
	gl_renderer = glGetString_fp (GL_RENDERER);
	Con_Printf ("GL_RENDERER: %s\n", gl_renderer);

	gl_version = glGetString_fp (GL_VERSION);
	Con_Printf ("GL_VERSION: %s\n", gl_version);
	gl_extensions = glGetString_fp (GL_EXTENSIONS);
	Con_DPrintf ("GL_EXTENSIONS: %s\n", gl_extensions);

	glGetIntegerv_fp(GL_MAX_TEXTURE_SIZE, &gl_max_size);
	if (gl_max_size < 256)	// Refuse to work when less than 256
		Sys_Error ("hardware capable of min. 256k opengl texture size needed");
	if (gl_max_size > 1024)	// We're cool with 1024, write a cmdline override if necessary
		gl_max_size = 1024;
	Con_Printf("OpenGL max.texture size: %i\n", gl_max_size);

	is_3dfx = false;
	if (!Q_strncasecmp ((char *)gl_renderer, "3dfx",  4)  ||
	    !Q_strncasecmp ((char *)gl_renderer, "Glide", 5)  ||
	    !Q_strncasecmp ((char *)gl_renderer, "Mesa Glide", 10))
	{
		Con_Printf("3dfx Voodoo found\n");
		is_3dfx = true;
	}

	if (Q_strncasecmp(gl_renderer,"PowerVR",7)==0)
		fullsbardraw = true;

	VID_InitGamma ();

	CheckMultiTextureExtensions ();
	CheckStencilBuffer();
	GL_InitLightmapBits();

	glClearColor_fp (1,0,0,0);
	glCullFace_fp(GL_FRONT);
	glEnable_fp(GL_TEXTURE_2D);

	glEnable_fp(GL_ALPHA_TEST);
	glAlphaFunc_fp(GL_GREATER, 0.632); // 1 - e^-1 : replaced 0.666 to avoid clipping of smaller fonts/graphics

	glPolygonMode_fp (GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel_fp (GL_FLAT);

	glTexParameterf_fp(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf_fp(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// previously GL_CLAMP was GL_REPEAT S.A
	glTexParameterf_fp(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf_fp(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	glBlendFunc_fp (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

//	glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}


/*
=================
GL_BeginRendering

=================
*/
void GL_BeginRendering (int *x, int *y, int *width, int *height)
{
	*x = *y = 0;
	*width = WindowRect.right - WindowRect.left;
	*height = WindowRect.bottom - WindowRect.top;

//	if (!wglMakeCurrent_fp( maindc, baseRC ))
//		Sys_Error ("wglMakeCurrent failed");

//	glViewport_fp (*x, *y, *width, *height);
}


void GL_EndRendering (void)
{
	if (!scr_skipupdate)
		SwapBuffers(maindc);

// handle the mouse state when windowed if that's changed
	if (modestate == MS_WINDOWED)
	{
		if ((int)_enable_mouse.value != enable_mouse)
		{
			if (_enable_mouse.value)
			{
				IN_ActivateMouse ();
				IN_HideMouse ();
			}
			else
			{
				IN_DeactivateMouse ();
				IN_ShowMouse ();
			}

			enable_mouse = (int)_enable_mouse.value;
		}
	}
	if (fullsbardraw)
		Sbar_Changed();
}


int ColorIndex[16] =
{
	0, 31, 47, 63, 79, 95, 111, 127, 143, 159, 175, 191, 199, 207, 223, 231
};

unsigned ColorPercent[16] =
{
	25, 51, 76, 102, 114, 127, 140, 153, 165, 178, 191, 204, 216, 229, 237, 247
};

#ifdef DO_BUILD
// these two procedures should have been used by Raven to
// generate the gfx/invpal.lmp file which resides in pak0
static int ConvertTrueColorToPal (unsigned char *true_color, unsigned char *palette)
{
	int	i;
	long	min_dist;
	int	min_index;
	long	r, g, b;

	min_dist = 256 * 256 + 256 * 256 + 256 * 256;
	min_index = -1;
	r = ( long )true_color[0];
	g = ( long )true_color[1];
	b = ( long )true_color[2];

	for (i = 0; i < 256; i++)
	{
		long palr, palg, palb, dist;
		long dr, dg, db;

		palr = palette[3*i];
		palg = palette[3*i+1];
		palb = palette[3*i+2];
		dr = palr - r;
		dg = palg - g;
		db = palb - b;
		dist = dr * dr + dg * dg + db * db;
		if (dist < min_dist)
		{
			min_dist = dist;
			min_index = i;
		}
	}
	return min_index;
}

static void VID_CreateInversePalette (unsigned char *palette)
{
	FILE	*FH;
	long	r, g, b;
	long	index = 0;
	unsigned char	true_color[3];
	char	path[MAX_OSPATH];

	for (r = 0; r < ( 1 << INVERSE_PAL_R_BITS ); r++)
	{
		for (g = 0; g < ( 1 << INVERSE_PAL_G_BITS ); g++)
		{
			for (b = 0; b < ( 1 << INVERSE_PAL_B_BITS ); b++)
			{
				true_color[0] = ( unsigned char )( r << ( 8 - INVERSE_PAL_R_BITS ) );
				true_color[1] = ( unsigned char )( g << ( 8 - INVERSE_PAL_G_BITS ) );
				true_color[2] = ( unsigned char )( b << ( 8 - INVERSE_PAL_B_BITS ) );
				inverse_pal[index] = ConvertTrueColorToPal( true_color, palette );
				index++;
			}
		}
	}

	snprintf (path, MAX_OSPATH, "%s/data1/gfx", com_basedir);
	Sys_mkdir (path);
	snprintf (path, MAX_OSPATH, "%s/data1/gfx/invpal.lmp", com_basedir);
	FH = fopen(path, "wb");
	if (!FH)
		Sys_Error ("Couldn't create %s", path);
	//fwrite (inverse_pal, 1, sizeof(inverse_pal), FH);
	fwrite (inverse_pal, 1, (sizeof(inverse_pal))-1, FH);
	fclose (FH);
	Con_Printf ("Created %s\n", path);
}
#endif


void VID_SetPalette (unsigned char *palette)
{
	byte	*pal;
	unsigned short	r,g,b;
	int		v;
	unsigned short	i, p, c;
	unsigned	*table;
//	unsigned	*table3dfx;
#ifndef OLD_8_BIT_PALETTE_CODE
	int		r1,g1,b1;
	int		j,k,l,m;
	FILE	*f;
	char	s[MAX_OSPATH];
#if !defined(NO_SPLASHES)
	HWND		hDlg, hProgress;
#endif
#endif
	static qboolean	been_here = false;

//
// 8 8 8 encoding
//
	pal = palette;
	table = d_8to24table;
//	table3dfx = d_8to24table3dfx;
	for (i=0 ; i<256 ; i++)
	{
		r = pal[0];
		g = pal[1];
		b = pal[2];
		pal += 3;

		v = (255<<24) + (r<<0) + (g<<8) + (b<<16);
		*table++ = v;
//		v = (255<<24) + (r<<16) + (g<<8) + (b<<0);
//		*table3dfx++ = v;
	}

	d_8to24table[255] &= 0xffffff;	// 255 is transparent

	pal = palette;
	table = d_8to24TranslucentTable;

	for (i=0; i<16;i++)
	{
		c = ColorIndex[i]*3;

		r = pal[c];
		g = pal[c+1];
		b = pal[c+2];

		for(p=0;p<16;p++)
		{
			v = (ColorPercent[15-p]<<24) + (r<<0) + (g<<8) + (b<<16);
			//v = (255<<24) + (r<<0) + (g<<8) + (b<<16);
			*table++ = v;

			RTint[i*16+p] = ((float)r) / ((float)ColorPercent[15-p]);
			GTint[i*16+p] = ((float)g) / ((float)ColorPercent[15-p]);
			BTint[i*16+p] = ((float)b) / ((float)ColorPercent[15-p]);
		}
	}

	// Initialize the palettized textures data
	if (been_here)
		return;

#ifdef OLD_8_BIT_PALETTE_CODE
	// This is original hexen2 code for palettized textures
	// Hexenworld replaced it with quake's newer code below
#   ifdef DO_BUILD
	VID_CreateInversePalette (palette);
#   else
	COM_LoadStackFile ("gfx/invpal.lmp", inverse_pal, sizeof(inverse_pal));
#   endif

#else // end of OLD_8_BIT_PALETTE_CODE
	COM_FOpenFile("glhexen/15to8.pal", &f, true);
	if (f)
	{
		fread(d_15to8table, 1<<15, 1, f);
		fclose(f);
	}
	else
	{	// JACK: 3D distance calcs:
		// k is last closest, l is the distance
#if !defined(NO_SPLASHES)
		hDlg = CreateDialog(global_hInstance, MAKEINTRESOURCE(IDD_PROGRESS), 
			NULL, NULL);
		hProgress = GetDlgItem(hDlg, IDC_PROGRESS);
		SendMessage(hProgress, PBM_SETSTEP, 1, 0);
		SendMessage(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 33));
#endif
		for (i=0,m=0; i < (1<<15); i++,m++)
		{
			/* Maps
			000000000000000
			000000000011111 = Red  = 0x1F
			000001111100000 = Blue = 0x03E0
			111110000000000 = Grn  = 0x7C00
			*/
			r = ((i & 0x1F) << 3)+4;
			g = ((i & 0x03E0) >> 2)+4;
			b = ((i & 0x7C00) >> 7)+4;
#   if 0
			r = (i << 11);
			g = (i << 6);
			b = (i << 1);
			r >>= 11;
			g >>= 11;
			b >>= 11;
#   endif
			pal = (unsigned char *)d_8to24table;
			for (v=0,k=0,l=10000; v<256; v++,pal+=4)
			{
				r1 = r-pal[0];
				g1 = g-pal[1];
				b1 = b-pal[2];
				j = sqrt(((r1*r1)+(g1*g1)+(b1*b1)));
				if (j<l)
				{
					k=v;
					l=j;
				}
			}
			d_15to8table[i]=k;
			if (m >= 1000)
			{
#if !defined(NO_SPLASHES)
#ifdef DEBUG_BUILD
				sprintf(s, "Done - %d\n", i);
				OutputDebugString(s);
#endif
				SendMessage(hProgress, PBM_STEPIT, 0, 0);
#endif
				m=0;
			}
		}
		sprintf(s, "%s/glhexen", com_gamedir);
		Sys_mkdir (s);
		sprintf(s, "%s/glhexen/15to8.pal", com_gamedir);
		f = fopen(s, "wb");
		if (f)
		{
			fwrite(d_15to8table, 1<<15, 1, f);
			fclose(f);
		}
#if !defined(NO_SPLASHES)
		DestroyWindow(hDlg);
#endif
	}
#endif	// end of new 8_BIT_PALETTE_CODE
	been_here = true;
}

void	VID_ShiftPalette (unsigned char *palette)
{
	if (gammaworks && SetDeviceGammaRamp_f)
		SetDeviceGammaRamp_f (maindc, ramps);
}


void	VID_Shutdown (void)
{
	HGLRC	hRC;
	HDC	hDC;

	if (vid_initialized)
	{
		vid_canalttab = false;
		hRC = wglGetCurrentContext_fp();
		hDC = wglGetCurrentDC_fp();

		if (maindc && gammaworks && SetDeviceGammaRamp_f)
			SetDeviceGammaRamp_f(maindc, orig_ramps);

		wglMakeCurrent_fp(NULL, NULL);

		if (hRC)
			wglDeleteContext_fp(hRC);

		if (hDC && dibwindow)
			ReleaseDC(dibwindow, hDC);

		if (modestate == MS_FULLDIB)
			ChangeDisplaySettings (NULL, 0);

		if (maindc && dibwindow)
			ReleaseDC (dibwindow, maindc);

		AppActivate(false, false);
#ifdef GL_DLSYM
		GL_CloseLibrary();
#endif
	}
}


//==========================================================================


static BOOL bSetupPixelFormat(HDC hDC)
{
	int pixelformat;

	if ( (pixelformat = ChoosePixelFormat(hDC, &pfd)) == 0 )
	{
		MessageBox(NULL, "ChoosePixelFormat failed", "Error", MB_OK);
		return FALSE;
	}

	if (SetPixelFormat(hDC, pixelformat, &pfd) == FALSE)
	{
		MessageBox(NULL, "SetPixelFormat failed", "Error", MB_OK);
		return FALSE;
	}

	return TRUE;
}



byte	scantokey[128] = { 
//  0           1       2       3       4       5       6       7 
//  8           9       A       B       C       D       E       F 
	0  ,    27,     '1',    '2',    '3',    '4',    '5',    '6', 
	'7',    '8',    '9',    '0',    '-',    '=',    K_BACKSPACE, 9, // 0 
	'q',    'w',    'e',    'r',    't',    'y',    'u',    'i', 
	'o',    'p',    '[',    ']',    13 ,    K_CTRL,'a',  's',      // 1 
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';', 
	'\'' ,    '`',    K_SHIFT,'\\',  'z',    'x',    'c',    'v',      // 2 
	'b',    'n',    'm',    ',',    '.',    '/',    K_SHIFT,'*', 
	K_ALT,' ',   0  ,    K_F1, K_F2, K_F3, K_F4, K_F5,   // 3 
	K_F6, K_F7, K_F8, K_F9, K_F10, K_PAUSE,    0  , K_HOME, 
	K_UPARROW,K_PGUP,'-',K_LEFTARROW,'5',K_RIGHTARROW,'+',K_END, //4 
	K_DOWNARROW,K_PGDN,K_INS,K_DEL,0,0,             0,              K_F11, 
	K_F12,0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,        // 5 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0, 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,        // 6 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0, 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0         // 7 
};

byte	shiftscantokey[128] = { 
//  0           1       2       3       4       5       6       7 
//  8           9       A       B       C       D       E       F 
	0  ,    27,     '!',    '@',    '#',    '$',    '%',    '^', 
	'&',    '*',    '(',    ')',    '_',    '+',    K_BACKSPACE, 9, // 0 
	'Q',    'W',    'E',    'R',    'T',    'Y',    'U',    'I', 
	'O',    'P',    '{',    '}',    13 ,    K_CTRL,'A',  'S',      // 1 
	'D',    'F',    'G',    'H',    'J',    'K',    'L',    ':', 
	'"' ,    '~',    K_SHIFT,'|',  'Z',    'X',    'C',    'V',      // 2 
	'B',    'N',    'M',    '<',    '>',    '?',    K_SHIFT,'*', 
	K_ALT,' ',   0  ,    K_F1, K_F2, K_F3, K_F4, K_F5,   // 3 
	K_F6, K_F7, K_F8, K_F9, K_F10, K_PAUSE,    0  , K_HOME, 
	K_UPARROW,K_PGUP,'_',K_LEFTARROW,'%',K_RIGHTARROW,'+',K_END, //4 
	K_DOWNARROW,K_PGDN,K_INS,K_DEL,0,0,             0,              K_F11, 
	K_F12,0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,        // 5 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0, 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,        // 6 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0, 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0         // 7 
};


/*
=======
MapKey

Map from windows to quake keynums
=======
*/
static int MapKey (int key)
{
	key = (key>>16)&255;
	if (key > 127)
		return 0;
	if (scantokey[key] == 0)
		Con_DPrintf("key 0x%02x has no translation\n", key);
	return scantokey[key];
}

/*
===================================================================

MAIN WINDOW

===================================================================
*/

/*
================
ClearAllStates
================
*/
static void ClearAllStates (void)
{
	int		i;
	
// send an up event for each key, to make sure the server clears them all
	for (i=0 ; i<256 ; i++)
	{
		Key_Event (i, false);
	}

	Key_ClearStates ();
	IN_ClearStates ();
}

static void AppActivate(BOOL fActive, BOOL minimize)
/****************************************************************************
*
* Function:     AppActivate
* Parameters:   fActive - True if app is activating
*
* Description:  If the application is activating, then swap the system
*               into SYSPAL_NOSTATIC mode so that our palettes will display
*               correctly.
*
****************************************************************************/
{
	static BOOL	sound_active;

	ActiveApp = fActive;
	Minimized = minimize;

// enable/disable sound on focus gain/loss
	if (!ActiveApp && sound_active)
	{
		S_BlockSound ();
		sound_active = false;
	}
	else if (ActiveApp && !sound_active)
	{
		S_UnblockSound ();
		sound_active = true;
	}

	if (fActive)
	{
		if (modestate == MS_FULLDIB)
		{
			IN_ActivateMouse ();
			IN_HideMouse ();
			if (vid_canalttab && vid_wassuspended) {
				vid_wassuspended = false;
				ChangeDisplaySettings (&gdevmode, CDS_FULLSCREEN);
				ShowWindow(mainwindow, SW_SHOWNORMAL);
				// Fix for alt-tab bug in NVidia drivers, from quakeforge
				MoveWindow(mainwindow, 0, 0, gdevmode.dmPelsWidth,
						gdevmode.dmPelsHeight, false);
			}
		}
		else if ((modestate == MS_WINDOWED) && _enable_mouse.value)
		{
			IN_ActivateMouse ();
			IN_HideMouse ();
		}
		VID_ShiftPalette(NULL);
	}

	if (!fActive)
	{
		if (maindc && gammaworks && SetDeviceGammaRamp_f)
		{
			SetDeviceGammaRamp_f(maindc, orig_ramps);
		}
		if (modestate == MS_FULLDIB)
		{
			IN_DeactivateMouse ();
			IN_ShowMouse ();
			if (vid_canalttab) { 
				ChangeDisplaySettings (NULL, 0);
				vid_wassuspended = true;
			}
		}
		else if ((modestate == MS_WINDOWED) && _enable_mouse.value)
		{
			IN_DeactivateMouse ();
			IN_ShowMouse ();
		}
	}
}


static int MWheelAccumulator;
static UINT  uMSG_MOUSEWHEEL;
extern cvar_t mwheelthreshold;
extern LONG CDAudio_MessageHandler(HWND,UINT,WPARAM,LPARAM);

/* main window procedure */
LONG WINAPI MainWndProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LONG	lRet = 0;
	int	fActive, fMinimized, temp;

	if (uMSG_MOUSEWHEEL && uMsg == uMSG_MOUSEWHEEL && mwheelthreshold.value >= 1)
	{	// win95 and nt-3.51 code. raven's original,
		// keeping it here for reference
		MWheelAccumulator += *(int *)&wParam;
		while (MWheelAccumulator >= mwheelthreshold.value)
		{
			Key_Event(K_MWHEELUP, true);
			Key_Event(K_MWHEELUP, false);
			MWheelAccumulator -= mwheelthreshold.value;
		}
		while (MWheelAccumulator <= -mwheelthreshold.value)
		{
			Key_Event(K_MWHEELDOWN, true);
			Key_Event(K_MWHEELDOWN, false);
			MWheelAccumulator += mwheelthreshold.value;
		}
		return DefWindowProc (hWnd, uMsg, wParam, lParam);
	}

	switch (uMsg)
	{
		case WM_KILLFOCUS:
			if (modestate == MS_FULLDIB)
				ShowWindow(mainwindow, SW_SHOWMINNOACTIVE);
			break;

		case WM_CREATE:
			if (Win95)
			{
				uMSG_MOUSEWHEEL = RegisterWindowMessage("MSWHEEL_ROLLMSG");
				if (!uMSG_MOUSEWHEEL)
					Con_Printf ("couldn't register mousewheel\n");
			}
			break;

		case WM_MOVE:
			if (modestate == MS_FULLDIB)
				break;	// ignore when fullscreen
			window_x = (int) LOWORD(lParam);
			window_y = (int) HIWORD(lParam);
			VID_UpdateWindowStatus ();
			break;

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			Key_Event (MapKey(lParam), true);
			break;
			
		case WM_KEYUP:
		case WM_SYSKEYUP:
			Key_Event (MapKey(lParam), false);
			break;

		case WM_SYSCHAR:
		// keep Alt-Space from happening
			break;

	// this is complicated because Win32 seems to pack multiple mouse events into
	// one update sometimes, so we always check all states and look for events
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MOUSEMOVE:
			temp = 0;

			if (wParam & MK_LBUTTON)
				temp |= 1;

			if (wParam & MK_RBUTTON)
				temp |= 2;

			if (wParam & MK_MBUTTON)
				temp |= 4;

			IN_MouseEvent (temp);

			break;

		case WM_MOUSEWHEEL:
			if ((short) HIWORD(wParam) > 0)
			{
				Key_Event(K_MWHEELUP, true);
				Key_Event(K_MWHEELUP, false);
			}
			else
			{
				Key_Event(K_MWHEELDOWN, true);
				Key_Event(K_MWHEELDOWN, false);
			}
			return 0;

		case WM_SIZE:
			break;

		case WM_CLOSE:
			if (MessageBox (mainwindow, "Are you sure you want to quit?", "Confirm Exit",
						MB_YESNO | MB_SETFOREGROUND | MB_ICONQUESTION) == IDYES)
			{
				Sys_Quit ();
			}

			break;

		case WM_ACTIVATE:
			fActive = LOWORD(wParam);
			fMinimized = (BOOL) HIWORD(wParam);
			AppActivate(!(fActive == WA_INACTIVE), fMinimized);

		// fix the leftover Alt from any Alt-Tab or the like that switched us away
			ClearAllStates ();

			break;

#if 0	// default handling for destroy, because we manually DestroyWindow() on mode changes
		case WM_DESTROY:
			{
				if (dibwindow)
					DestroyWindow (dibwindow);

				PostQuitMessage (0);
			}
			break;
#endif

		case MM_MCINOTIFY:
			lRet = CDAudio_MessageHandler (hWnd, uMsg, wParam, lParam);
			break;

		default:
			/* pass all unhandled messages to DefWindowProc */
			lRet = DefWindowProc (hWnd, uMsg, wParam, lParam);
			break;
	}

	/* return 1 if handled message, 0 if not */
	return lRet;
}


/*
=================
VID_NumModes
=================
*/
static int VID_NumModes (void)
{
	return *nummodes;
}


/*
=================
VID_GetModePtr
=================
*/
static vmode_t *VID_GetModePtr (int modenum)
{
	if ((modenum >= 0) && (modenum < *nummodes))
		return &modelist[modenum];
	else
		return &badmode;
}


/*
=================
VID_GetModeDescription
=================
*/
static char *VID_GetModeDescription (int mode)
{
	char		*pinfo;
	vmode_t		*pv;

	if ((mode < 0) || (mode >= *nummodes))
		return NULL;

	pv = VID_GetModePtr (mode);
	pinfo = pv->modedesc;

	return pinfo;
}


// KJB: Added this to return the mode driver name in description for console

static char *VID_GetExtModeDescription (int mode)
{
	static char	pinfo[100];
	vmode_t		*pv;

	if ((mode < 0) || (mode >= *nummodes))
		return NULL;

	pv = VID_GetModePtr (mode);
	if (modelist[mode].type == MS_FULLDIB)
	{
		sprintf(pinfo,"%s fullscreen", pv->modedesc);
	}
	else
	{
		if (modestate == MS_WINDOWED)
			sprintf(pinfo, "%s windowed", pv->modedesc);
		else
			sprintf(pinfo, "windowed");
	}

	return pinfo;
}


/*
=================
VID_DescribeCurrentMode_f
=================
*/
static void VID_DescribeCurrentMode_f (void)
{
	Con_Printf ("%s\n", VID_GetExtModeDescription (vid_modenum));
}


/*
=================
VID_NumModes_f
=================
*/
static void VID_NumModes_f (void)
{
	if (*nummodes == 1)
		Con_Printf ("1 video mode is available\n");
	else
		Con_Printf ("%d video modes are available\n", *nummodes);
}


/*
=================
VID_DescribeMode_f
=================
*/
static void VID_DescribeMode_f (void)
{
	int	modenum;

	modenum = atoi (Cmd_Argv(1));

	Con_Printf ("%s\n", VID_GetExtModeDescription (modenum));
}

/*
=================
VID_DescribeModes_f
=================
*/
static void VID_DescribeModes_f (void)
{
	int	i, lnummodes;
	char	*pinfo;
	vmode_t	*pv;

	lnummodes = VID_NumModes ();

	for (i=0 ; i<lnummodes ; i++)
	{
		pv = VID_GetModePtr (i);
		pinfo = VID_GetExtModeDescription (i);
		Con_Printf ("%2d: %s\n", i, pinfo);
	}
}


static void VID_RegisterWndClass(HINSTANCE hInstance)
{
	WNDCLASS	wc;

	wc.style         = 0;
	wc.lpfnWndProc   = (WNDPROC)MainWndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = hInstance;
	wc.hIcon         = 0;
	wc.hCursor       = LoadCursor (NULL,IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszMenuName  = 0;
#ifndef H2W
	wc.lpszClassName = "HexenII";
#else
	wc.lpszClassName = "HexenWorld";
#endif

	if (!RegisterClass(&wc))
		Sys_Error ("Couldn't register main window class");
}

static void VID_InitDIB (HINSTANCE hInstance)
{
	int		i;
	HDC	tempDC;

	// get desktop settings, hope that the user don't do a silly
	// thing like going back to desktop and changing its settings
	//vid_deskwidth = GetSystemMetrics (SM_CXSCREEN);
	//vid_deskheight = GetSystemMetrics (SM_CXSCREEN);
	tempDC = GetDC (GetDesktopWindow());
	vid_deskwidth = GetDeviceCaps (tempDC, HORZRES);
	vid_deskheight = GetDeviceCaps(tempDC, VERTRES);
	vid_deskbpp = GetDeviceCaps (tempDC, BITSPIXEL);
	ReleaseDC (GetDesktopWindow(), tempDC);

	// refuse to run if vid_deskbpp < 15
	if (vid_deskbpp < 15)
		Sys_Error ("Desktop color depth too low\n"
			   "Make sure you are running at 16 bpp or better");

	/* Register the frame class */
	VID_RegisterWndClass(hInstance);

	// initialize standart windowed modes list
	num_wmodes = 0;

	for (i = 0, num_wmodes = 0; i < MAX_STDMODES; i++)
	{
		if (std_modes[i].width <= vid_deskwidth && std_modes[i].height <= vid_deskheight)
		{
			wmodelist[num_wmodes].type = MS_WINDOWED;
			wmodelist[num_wmodes].width = std_modes[i].width;
			wmodelist[num_wmodes].height = std_modes[i].height;
			sprintf (wmodelist[num_wmodes].modedesc, "%dx%d",
				 wmodelist[num_wmodes].width, wmodelist[num_wmodes].height);
			wmodelist[num_wmodes].modenum = MODE_WINDOWED;
			wmodelist[num_wmodes].dib = 1;
			wmodelist[num_wmodes].fullscreen = 0;
			wmodelist[num_wmodes].halfscreen = 0;
			wmodelist[num_wmodes].bpp = 0;
			num_wmodes++;
		}
	}
}


/*
=================
VID_InitFullDIB
=================
*/
static void VID_InitFullDIB (HINSTANCE hInstance)
{
	DEVMODE	devmode;
	int	i, modenum, existingmode;
	int	j, bpp, done;
	BOOL	stat;

	num_fmodes = 0;

	modenum = 0;

	// enumerate >8 bpp modes
	do
	{
		stat = EnumDisplaySettings (NULL, modenum, &devmode);

		if ((devmode.dmBitsPerPel >= 15) &&
			(devmode.dmPelsWidth <= MAXWIDTH) &&
			(devmode.dmPelsHeight <= MAXHEIGHT) &&
			(num_fmodes < MAX_MODE_LIST))
		{
			devmode.dmFields = DM_BITSPERPEL |
					   DM_PELSWIDTH |
					   DM_PELSHEIGHT;

			if (ChangeDisplaySettings (&devmode, CDS_TEST | CDS_FULLSCREEN) ==
					DISP_CHANGE_SUCCESSFUL)
			{
				fmodelist[num_fmodes].type = MS_FULLDIB;
				fmodelist[num_fmodes].width = devmode.dmPelsWidth;
				fmodelist[num_fmodes].height = devmode.dmPelsHeight;
				fmodelist[num_fmodes].modenum = 0;
				fmodelist[num_fmodes].halfscreen = 0;
				fmodelist[num_fmodes].dib = 1;
				fmodelist[num_fmodes].fullscreen = 1;
				fmodelist[num_fmodes].bpp = devmode.dmBitsPerPel;
				sprintf (fmodelist[num_fmodes].modedesc, "%dx%dx%d",
						(int)devmode.dmPelsWidth, (int)devmode.dmPelsHeight,
						(int)devmode.dmBitsPerPel);

			// if the width is more than twice the height, reduce it by half because this
			// is probably a dual-screen monitor
				if (!COM_CheckParm("-noadjustaspect"))
				{
					if (fmodelist[num_fmodes].width > (fmodelist[num_fmodes].height << 1))
					{
						fmodelist[num_fmodes].width >>= 1;
						fmodelist[num_fmodes].halfscreen = 1;
						sprintf (fmodelist[num_fmodes].modedesc, "%dx%dx%d",
								 fmodelist[num_fmodes].width,
								 fmodelist[num_fmodes].height,
								 fmodelist[num_fmodes].bpp);
					}
				}

				for (i=0, existingmode = 0 ; i<num_fmodes ; i++)
				{
					if ((fmodelist[num_fmodes].width == fmodelist[i].width)   &&
						(fmodelist[num_fmodes].height == fmodelist[i].height) &&
						(fmodelist[num_fmodes].bpp == fmodelist[i].bpp))
					{
						existingmode = 1;
						break;
					}
				}

				if (!existingmode)
				{
					num_fmodes++;
				}
			}
		}

		modenum++;
	} while (stat);

	// see if there are any low-res modes that aren't being reported
	bpp = 16;
	done = 0;

	do
	{
		for (j = 0; (j < NUM_LOWRESMODES) && (num_fmodes < MAX_MODE_LIST); j++)
		{
			devmode.dmBitsPerPel = bpp;
			devmode.dmPelsWidth = std_modes[j].width;
			devmode.dmPelsHeight = std_modes[j].height;
			devmode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;

			if (ChangeDisplaySettings (&devmode, CDS_TEST | CDS_FULLSCREEN) ==
					DISP_CHANGE_SUCCESSFUL)
			{
				fmodelist[num_fmodes].type = MS_FULLDIB;
				fmodelist[num_fmodes].width = devmode.dmPelsWidth;
				fmodelist[num_fmodes].height = devmode.dmPelsHeight;
				fmodelist[num_fmodes].modenum = 0;
				fmodelist[num_fmodes].halfscreen = 0;
				fmodelist[num_fmodes].dib = 1;
				fmodelist[num_fmodes].fullscreen = 1;
				fmodelist[num_fmodes].bpp = devmode.dmBitsPerPel;
				sprintf (fmodelist[num_fmodes].modedesc, "%dx%dx%d",
						(int)devmode.dmPelsWidth, (int)devmode.dmPelsHeight,
						(int)devmode.dmBitsPerPel);

				for (i=0, existingmode = 0 ; i<num_fmodes ; i++)
				{
					if ((fmodelist[num_fmodes].width == fmodelist[i].width)   &&
						(fmodelist[num_fmodes].height == fmodelist[i].height) &&
						(fmodelist[num_fmodes].bpp == fmodelist[i].bpp))
					{
						existingmode = 1;
						break;
					}
				}

				if (!existingmode)
				{
					num_fmodes++;
				}
			}
		}
		switch (bpp)
		{
			case 16:
				bpp = 32;
				break;

			case 32:
#if 0	// O.S: don't mess with silly 24 bit lowres modes, they don't work correctly
				bpp = 24;
				break;

			case 24:
#endif
				done = 1;
				break;
		}
	} while (!done);

	if (num_fmodes == 0)
		Con_SafePrintf ("No fullscreen DIB modes found\n");
}

static void VID_Init8bitPalette (void)
{
	// Check for 8bit Extensions and initialize them.
	int	i;
	char	thePalette[256*3];
	char	*oldPalette, *newPalette;

	MyglColorTableEXT = (void *)wglGetProcAddress_fp("glColorTableEXT");
	if (MyglColorTableEXT && strstr(gl_extensions, "GL_EXT_shared_texture_palette"))
	{
		Con_SafePrintf("8-bit GL extensions enabled.\n");
		glEnable_fp( GL_SHARED_TEXTURE_PALETTE_EXT );
		oldPalette = (char *) d_8to24table; //d_8to24table3dfx;
		newPalette = thePalette;
		for (i=0;i<256;i++) {
			*newPalette++ = *oldPalette++;
			*newPalette++ = *oldPalette++;
			*newPalette++ = *oldPalette++;
			oldPalette++;
		}
		MyglColorTableEXT(GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGB, 256, GL_RGB, GL_UNSIGNED_BYTE,
			(void *) thePalette);
		is8bit = TRUE;
	}
}

static void VID_ChangeVideoMode(int newmode)
{
	int j, temp, temp2;

	// Avoid window updates and alt+tab handling (which sets modes back)
	temp = scr_disabled_for_loading;
	temp2 = vid_canalttab;
	scr_disabled_for_loading = true;
	vid_canalttab = false;

	// restore gamma, just in case
	if (maindc && gammaworks && SetDeviceGammaRamp_f)
		SetDeviceGammaRamp_f(maindc, orig_ramps);
	CDAudio_Pause ();
	MIDI_Pause(2);
	S_ClearBuffer ();

	// Unload all textures and reset texture counts
	D_ClearOpenGLTextures(0);
	texture_extension_number = 1;
	lightmap_textures = 0;
	for (j = 0; j < MAX_LIGHTMAPS; j++)
		lightmap_modified[j] = true;
	// reset all function pointers
	GL_ResetFunctions();

	// Kill device and rendering contexts
	wglMakeCurrent_fp(NULL, NULL);
	if (baseRC)
		wglDeleteContext_fp(baseRC);
	baseRC = 0;
	if (maindc && mainwindow)
		ReleaseDC(mainwindow, maindc);
	maindc = 0;
	// Destroy main window and unregister its class
	if (mainwindow)
	{
		ShowWindow(mainwindow, SW_HIDE);
		DestroyWindow(mainwindow);
	}
	mainwindow = dibwindow = 0;
#ifndef H2W
	UnregisterClass("HexenII", global_hInstance);
#else
	UnregisterClass("HexenWorld", global_hInstance);
#endif

#ifdef GL_DLSYM
	// reload the opengl library
	GL_CloseLibrary();
	if (!GL_OpenLibrary(gl_library))
		Sys_Error ("Unable to load GL library %s", gl_library);
#endif

	// Register main window class and create main window
	VID_RegisterWndClass(global_hInstance);
	VID_SetMode(newmode, host_basepal);
	// Obtain device context and set up pixel format
	maindc = GetDC(mainwindow);
	bSetupPixelFormat(maindc);

	// Create OpenGL rendering context and make it current
	baseRC = wglCreateContext_fp(maindc);
	if (!baseRC)
		Sys_Error("wglCreateContext failed");
	if (!wglMakeCurrent_fp(maindc, baseRC ))
		Sys_Error("wglMakeCurrent failed");

	CDAudio_Resume ();
	MIDI_Pause(1);

	// Reload graphics wad file (Draw_PicFromWad writes glpic_t data (sizes,
	// texnums) right on top of the original pic data, so the pic data will
	// be dirty after gl textures are loaded the first time; we need to load
	// a clean version)
	W_LoadWadFile ("gfx.wad");
	// Avoid re-registering commands and re-allocating memory
	draw_reinit = true;
	// Initialize extensions and default OpenGL parameters
	GL_Init();
	// Check for 3DFX Extensions and initialize them.
	if (COM_CheckParm("-paltex"))
		VID_Init8bitPalette();

	// Reload pre-map pics, fonts, console, etc
	Draw_Init();
	SCR_Init();
	Sbar_Init();
	vid.recalc_refdef = 1;
	// Reload the particle texture
	R_InitParticleTexture();
	// Reload model textures and player skins
	Mod_ReloadTextures();
	// rebuild the lightmaps
	GL_BuildLightmaps();
	draw_reinit = false;
	scr_disabled_for_loading = temp;
	VID_ShiftPalette(NULL);
	vid_canalttab = temp2;
}

static void VID_Restart_f (void)
{
	if ((int)vid_mode.value < 0 || (int)vid_mode.value >= *nummodes)
	{
		Con_Printf ("Bad video mode %d\n", (int)vid_mode.value);
		Cvar_SetValue ("vid_mode", vid_modenum);
		return;
	}

	Con_Printf ("Re-initializing video:\n");
	VID_ChangeVideoMode ((int)vid_mode.value);
}

static int sort_modes (const void *arg1, const void *arg2)
{
	const vmode_t *a1, *a2;
	a1 = (vmode_t *) arg1;
	a2 = (vmode_t *) arg2;

	if (a1->bpp == a2->bpp)
	{
		if (a1->width == a2->width)
		{
			return a1->height - a2->height;	// lowres-to-highres
		//	return a2->height - a1->height;	// highres-to-lowres
		}
		else
		{
			return a1->width - a2->width;	// lowres-to-highres
		//	return a2->width - a1->width;	// highres-to-lowres
		}
	}
	else
	{
		return a1->bpp - a2->bpp;	// low bpp-to-high bpp
	//	return a2->bpp - a1->bpp;	// high bpp-to-low bpp
	}
}

static void VID_SortModes (void)
{
	int	i, j;

	if (num_fmodes == 0)
		return;

	// sort the fullscreen modes list
	qsort(fmodelist, num_fmodes, sizeof fmodelist[0], sort_modes);
	// find which bpp values are reported to us
	for (i=0 ; i<MAX_NUMBPP ; i++)
	{
		bpplist[i][0] = 0;
		bpplist[i][1] = 0;
	}
	bpplist[0][0] = fmodelist[0].bpp;
	bpplist[0][1] = 0;
	for (i=1, j=0; i < num_fmodes && j < MAX_NUMBPP; i++)
	{
		if (fmodelist[i-1].bpp != fmodelist[i].bpp)
		{
			bpplist[++j][0] = fmodelist[i].bpp;
			bpplist[j][1] = i;
		}
	}

	vid_deskmode = -1;

	// find the desktop mode number. shouldn't fail!
	for (i=1; i < num_fmodes ; i++)
	{
		if ((fmodelist[i].width == vid_deskwidth) &&
			(fmodelist[i].height == vid_deskheight) &&
			(fmodelist[i].bpp == vid_deskbpp))
		{
			vid_deskmode = i;
			break;
		}
	}
	if (vid_deskmode < 0)
		Con_Printf ("WARNING: desktop resolution not found in modelist");
}

/*
===================
VID_Init
===================
*/
void	VID_Init (unsigned char *palette)
{
	int	i, j, existingmode;
	qboolean	usermode = false;
	int	width, height, bpp, zbits, findbpp, done;
	char	gldir[MAX_OSPATH];
	HDC	hdc;

	Cvar_RegisterVariable (&vid_mode);
	Cvar_RegisterVariable (&_enable_mouse);
	Cvar_RegisterVariable (&gl_ztrick);
	Cvar_RegisterVariable (&gl_purge_maptex);

	Cmd_AddCommand ("vid_nummodes", VID_NumModes_f);
	Cmd_AddCommand ("vid_describecurrentmode", VID_DescribeCurrentMode_f);
	Cmd_AddCommand ("vid_describemode", VID_DescribeMode_f);
	Cmd_AddCommand ("vid_describemodes", VID_DescribeModes_f);
	Cmd_AddCommand ("vid_restart", VID_Restart_f);

	// prepare directories for caching mesh files
	sprintf (gldir, "%s/glhexen", com_gamedir);
	Sys_mkdir (gldir);
	sprintf (gldir, "%s/glhexen/boss", com_gamedir);
	Sys_mkdir (gldir);
	sprintf (gldir, "%s/glhexen/puzzle", com_gamedir);
	Sys_mkdir (gldir);

	hIcon = LoadIcon (global_hInstance, MAKEINTRESOURCE (IDI_ICON2));

#if !defined(NO_SPLASHES)
	InitCommonControls();
	VID_SetPalette (palette);
#endif

#ifdef GL_DLSYM
	i = COM_CheckParm("-gllibrary");
	if (i && i < com_argc - 1)
		gl_library = com_argv[i+1];
	else
		gl_library = "opengl32.dll";
	if (!GL_OpenLibrary(gl_library))
		Sys_Error ("Unable to load GL library %s", gl_library);
#endif

	VID_InitDIB (global_hInstance);

	VID_InitFullDIB (global_hInstance);

	Con_Printf ("Desktop settings: %d x %d x %d\n", vid_deskwidth, vid_deskheight, vid_deskbpp);

	// sort the modes
	VID_SortModes();

	if (COM_CheckParm("-window") || COM_CheckParm("-w"))
	{
		hdc = GetDC (NULL);
		if (GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE)
		{
			Sys_Error ("Can't run in non-RGB mode");
		}
		ReleaseDC (NULL, hdc);

		windowed = true;

		modelist = (vmode_t *)wmodelist;
		nummodes = &num_wmodes;
		vid_default = RES_640X480;

		// start parsing any dimension request from user
		i = COM_CheckParm("-width");
		if (i && i < com_argc-1)
		{
			i = atoi(com_argv[COM_CheckParm("-width")+1]);
			// don't allow requests larger than desktop
			if (i <= vid_deskwidth)
			{
				if (i < 320)
					i = 320;
				wmodelist[num_wmodes].width = i;
				wmodelist[num_wmodes].height = wmodelist[num_wmodes].width * 3 / 4;
				i = COM_CheckParm("-height");
				if (i && i < com_argc-1)
				{
					i = atoi(com_argv[COM_CheckParm("-height")+1]);
					if (i <= vid_deskheight)
					{
						if (i < 240)
							i = 240;
						wmodelist[num_wmodes].height= i;
					}
				}

				wmodelist[num_wmodes].type = MS_WINDOWED;
				sprintf (wmodelist[num_wmodes].modedesc, "%dx%d",
					 wmodelist[num_wmodes].width, wmodelist[num_wmodes].height);
				wmodelist[num_wmodes].modenum = MODE_WINDOWED;
				wmodelist[num_wmodes].dib = 1;
				wmodelist[num_wmodes].fullscreen = 0;
				wmodelist[num_wmodes].halfscreen = 0;
				wmodelist[num_wmodes].bpp = 0;

				for (i=0, existingmode = 0; i < num_wmodes; i++)
				{
					if ((wmodelist[num_wmodes].width == wmodelist[i].width) &&
						(wmodelist[num_wmodes].height == wmodelist[i].height))
					{
						existingmode = 1;
						vid_default = i;
						break;
					}
				}

				if (!existingmode)
				{
					strcat (wmodelist[num_wmodes].modedesc, " (user mode)");
					vid_default = num_wmodes;
					num_wmodes++;
				}
			}
		}
		// end of parsing user request

	}
	else	// fullscreen, default
	{
		if (num_fmodes == 0)
		{
			Sys_Error ("No RGB fullscreen modes available");
		}

		windowed = false;

		modelist = (vmode_t *)fmodelist;
		nummodes = &num_fmodes;
		vid_default = -1;

		width = std_modes[RES_640X480].width;
		height = std_modes[RES_640X480].height;
		findbpp = 1;
		bpp = bpplist[0][0];
		if (Win95old)
		{	// don't bother with multiple bpp values on
			// windows versions older than win95-osr2.
			// in fact, should we stop supporting it?
			bpp = vid_deskbpp;
			findbpp = 0;
		}

		if (COM_CheckParm("-current"))
		{	// user wants fullscreen and
			// with desktop dimensions
			if (vid_deskmode >= 0)
			{
				strcat (modelist[vid_deskmode].modedesc, " (desktop)");
				vid_default = vid_deskmode;
			}
			else
			{
				Con_Printf ("WARNING: desktop mode not available for the -current switch");
			}
		}
		else
		{
			// start parsing any dimension/bpp request from user
			i = COM_CheckParm("-width");
			if (i && i < com_argc-1)
			{
				width = atoi(com_argv[i+1]);
				if (width < 320)
					width = 320;
				i = COM_CheckParm("-height");
				if (i && i < com_argc-1)
				{
					height = atoi(com_argv[i+1]);
					if (height < 240)
						height = 240;
				}
				else
				{	// proceed with 4/3 ratio
					height = 3 * width / 4;
				}
				usermode = true;
			}

			i = COM_CheckParm("-bpp");
			if (i && i < com_argc-1 && !Win95old)
			{
				bpp = atoi(com_argv[i+1]);
				findbpp = 0;
				usermode = true;
			}

			// if they want to force it, add the specified mode to the list
			if (COM_CheckParm("-force"))
			{
				fmodelist[num_fmodes].type = MS_FULLDIB;
				fmodelist[num_fmodes].width = width;
				fmodelist[num_fmodes].height = height;
				fmodelist[num_fmodes].modenum = 0;
				fmodelist[num_fmodes].halfscreen = 0;
				fmodelist[num_fmodes].dib = 1;
				fmodelist[num_fmodes].fullscreen = 1;
				fmodelist[num_fmodes].bpp = bpp;
				sprintf (fmodelist[num_fmodes].modedesc, "%dx%dx%d",
						fmodelist[num_fmodes].width, fmodelist[num_fmodes].height,
						fmodelist[num_fmodes].bpp);

				for (i=0, existingmode = 0 ; i<num_fmodes ; i++)
				{
					if ((fmodelist[num_fmodes].width == fmodelist[i].width)   &&
						(fmodelist[num_fmodes].height == fmodelist[i].height) &&
						(fmodelist[num_fmodes].bpp == modelist[i].bpp))
					{
						existingmode = 1;
						break;
					}
				}

				if (!existingmode)
				{
					if (usermode)
						strcat (fmodelist[num_fmodes].modedesc, " (user mode)");
					num_fmodes++;
					// re-sort the modes
					VID_SortModes();
					if (findbpp)
						bpp = bpplist[0][0];
				}
			}

			if (vid_deskmode >= 0)
				strcat (fmodelist[vid_deskmode].modedesc, " (desktop)");

			j = done = 0;

			do
			{
				for (i = 0; i < *nummodes; i++)
				{
					if ((modelist[i].width == width) &&
						(modelist[i].height == height) &&
						(modelist[i].bpp == bpp))
					{
						vid_default = i;
						done = 1;
						break;
					}
				}

				if (!done)
				{
					if (findbpp)
					{
						j++;
						if (i >= MAX_NUMBPP || !bpplist[j][0])
							done = 1;
						if (!done)
							bpp = bpplist[j][0];
					}
					else
					{
						done = 1;
					}
				}
			} while (!done);

			if (vid_default < 0)
			{
				Sys_Error ("Specified video mode not available");
			}

			//pfd.cColorBits = modelist[vid_default].bpp;

			i = COM_CheckParm("-zbits");
			if (i && i < com_argc-1)
			{
				zbits = atoi(com_argv[i+1]);
				if (zbits)
					pfd.cDepthBits = zbits;
			}
		}
	}	// end of fullscreen parsing

	vid_initialized = true;

	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));

#if !defined(NO_SPLASHES)
	DestroyWindow (hwnd_dialog);
#endif

	// so Con_Printfs don't mess us up by forcing vid and snd updates
	i = scr_disabled_for_loading;
	scr_disabled_for_loading = true;

	Cvar_SetValue ("vid_mode", vid_default);
	VID_SetMode (vid_default, palette);

	maindc = GetDC(mainwindow);
	bSetupPixelFormat(maindc);

	baseRC = wglCreateContext_fp( maindc );
	if (!baseRC)
		Sys_Error ("Could not initialize GL (wglCreateContext failed).\n\nMake sure you in are 65535 color mode, and try running -window.");
	if (!wglMakeCurrent_fp( maindc, baseRC ))
		Sys_Error ("wglMakeCurrent failed");

	GL_Init ();

	// set our palette
	VID_SetPalette (palette);

	// Check for 3DFX Extensions and initialize them.
	if (COM_CheckParm("-paltex"))
		VID_Init8bitPalette();

	scr_disabled_for_loading = i;
	vid.recalc_refdef = 1;

	vid_menudrawfn = VID_MenuDraw;
	vid_menukeyfn = VID_MenuKey;

	strcpy (badmode.modedesc, "Bad mode");
	vid_canalttab = true;

	if (COM_CheckParm("-fullsbar"))
		fullsbardraw = true;
}


void VID_ToggleFullscreen (void)
{
}


#ifndef H2W
//unused in hexenworld
void D_ShowLoadingSize(void)
{
	if (!vid_initialized)
		return;

	glDrawBuffer_fp (GL_FRONT);

	SCR_DrawLoading();

	glDrawBuffer_fp (GL_BACK);

	glFlush_fp();
}
#endif


//========================================================
// Video menu stuff
//========================================================

static int	vid_menunum;
static vmode_t	*vid_menulist;
static int	vid_menubpp;
static qboolean	vid_menu_fs;
static qboolean	want_fstoggle;
static int	vid_cursor = 0;
static qboolean	vid_menu_firsttime = true;

enum {
	VID_FULLSCREEN,
	VID_RESOLUTION,
	VID_BPP,
	VID_ITEMS
};


/*
================
VID_MenuDraw
================
*/
void VID_MenuDraw (void)
{
	ScrollTitle("gfx/menu/title7.lmp");

	if (vid_menu_firsttime)
	{	// settings for entering the menu first time
		vid_menunum = vid_modenum;
		vid_menubpp = modelist[vid_modenum].bpp;
		vid_menu_fs = (modestate != MS_WINDOWED);
		if (modestate == MS_WINDOWED)
			vid_menulist = (vmode_t *)wmodelist;
		else
			vid_menulist = (vmode_t *)fmodelist;
		vid_menu_firsttime = false;
	}
	// just in case...
	if ( vid_cursor == VID_BPP && (!vid_menu_fs || !num_fmodes || Win95old ))
		vid_cursor = VID_RESOLUTION;

	want_fstoggle = ( ((modestate == MS_WINDOWED) && vid_menu_fs) || ((modestate != MS_WINDOWED) && !vid_menu_fs) );

	M_Print (76, 92 + 8*VID_FULLSCREEN, "Fullscreen: ");
	if (want_fstoggle)
	{
		if (vid_menu_fs)
			M_Print (76+12*8, 92 + 8*VID_FULLSCREEN, "yes");
		else
			M_Print (76+12*8, 92 + 8*VID_FULLSCREEN, "no");
	}
	else
	{
		if (vid_menu_fs)
			M_PrintWhite (76+12*8, 92 + 8*VID_FULLSCREEN, "yes");
		else
			M_PrintWhite (76+12*8, 92 + 8*VID_FULLSCREEN, "no");
	}

	M_Print (76, 92 + 8*VID_RESOLUTION, "Resolution: ");
	if (vid_menunum == vid_modenum)
		M_PrintWhite (76+12*8, 92 + 8*VID_RESOLUTION, vid_menulist[vid_menunum].modedesc);
	else
		M_Print (76+12*8, 92 + 8*VID_RESOLUTION, vid_menulist[vid_menunum].modedesc);

	if (vid_menu_fs && num_fmodes && !Win95old)
	{
		M_Print (76, 92 + 8*VID_BPP, "Color BPP : ");
		if (vid_menubpp == modelist[vid_modenum].bpp)
			M_PrintWhite (76+12*8, 92 + 8*VID_BPP, va("%d",vid_menubpp));
		else
			M_Print (76+12*8, 92 + 8*VID_BPP, va("%d",vid_menubpp));
	}

	M_DrawCharacter (64, 92 + vid_cursor*8, 12+((int)(realtime*4)&1));
}

static int find_bppnum (int incr)
{
	int j, pos = -1;

	if (!vid_menu_fs)	// then it doesn't matter
		return 0;

	for (j=0; j<MAX_NUMBPP; j++)
	{	// find the pos in the bpplist
		if (vid_menubpp == bpplist[j][0])
		{
			pos = j;
			j = j+incr;
			break;
		}
	}
	if (pos < 0)
		Sys_Error ("bpp unexpectedly not found in list");
	if (incr == 0)
		return pos;
	// find the next available bpp
	while (1)
	{
		if (j>=MAX_NUMBPP)
			j = 0;
		if (j<0)
			j = MAX_NUMBPP-1;
		if (bpplist[j][0])
			break;
		j = j+incr;
	}
	return j;
}

static int match_mode_to_bpp (int bppnum)
{
	int k, l;

	k = bpplist[bppnum][1];
	l = bpplist[bppnum][1] + 1;
	for ( ; l < num_fmodes && fmodelist[l].bpp == vid_menubpp; l++)
	{
		if (fmodelist[vid_menunum].width == fmodelist[l].width &&
		    fmodelist[vid_menunum].height == fmodelist[l].height)
		{
			k = l;
			break;
		}
	}
	return k;
}

static int match_windowed_fullscr_modes (void)
{
	int	l;
	vmode_t	*tmplist;
	int	*tmpcount;

	// choose the new mode
	tmplist = (vid_menu_fs) ? (vmode_t *)fmodelist : (vmode_t *)wmodelist;
	tmpcount = (vid_menu_fs) ? &num_fmodes : &num_wmodes;
	for (l = 0 ; l < *tmpcount; l++)
	{
		if (tmplist[l].width == vid_menulist[vid_menunum].width &&
		    tmplist[l].height == vid_menulist[vid_menunum].height)
		{
			return l;
		}
	}
	return 0;
}

/*
================
VID_MenuKey
================
*/
void VID_MenuKey (int key)
{
	int	i;
	int	*tmpnum;

	switch (key)
	{
	case K_ESCAPE:
		M_Menu_Options_f ();
		break;

	case K_UPARROW:
		S_LocalSound ("raven/menu1.wav");
		vid_cursor--;
		if (vid_cursor < 0)
			vid_cursor = VID_ITEMS-1;
		if ( vid_cursor == VID_BPP && (!vid_menu_fs || !num_fmodes || Win95old ))
			vid_cursor--;
		break;

	case K_DOWNARROW:
		S_LocalSound ("raven/menu1.wav");
		vid_cursor++;
		if ( vid_cursor == VID_BPP && (!vid_menu_fs || !num_fmodes || Win95old ))
			vid_cursor++;
		if (vid_cursor >= VID_ITEMS)
			vid_cursor = 0;
		break;

	case K_ENTER:
		switch (vid_cursor)
		{
		case VID_FULLSCREEN:
		case VID_RESOLUTION:
		case VID_BPP:
			if (vid_menunum != vid_modenum || want_fstoggle)
			{
				Cvar_SetValue("vid_mode", vid_menunum);
				modelist = (vid_menu_fs) ? (vmode_t *)fmodelist : (vmode_t *)wmodelist;
				nummodes = (vid_menu_fs) ? &num_fmodes : &num_wmodes;
				VID_Restart_f();
				windowed = (modestate == MS_WINDOWED);
			}
			break;
		}
		return;

	case K_RIGHTARROW:
		switch (vid_cursor)
		{
		case VID_FULLSCREEN:
			if (!num_fmodes)
				break;
			vid_menu_fs = !vid_menu_fs;
			vid_menunum = match_windowed_fullscr_modes();
			vid_menulist = (vid_menu_fs) ? (vmode_t *)fmodelist : (vmode_t *)wmodelist;
			vid_menubpp = vid_menulist[vid_menunum].bpp;
			break;
		case VID_RESOLUTION:
			S_LocalSound ("raven/menu1.wav");
			tmpnum = (vid_menu_fs) ? &num_fmodes : &num_wmodes;
			vid_menunum++;
			if (vid_menunum >= *tmpnum || vid_menubpp != vid_menulist[vid_menunum].bpp)
				vid_menunum--;
			break;
		case VID_BPP:
			i = find_bppnum (1);
			if (vid_menubpp == bpplist[i][0])
				break;
			vid_menubpp = bpplist[i][0];
			//find a matching video mode for this bpp
			vid_menunum = match_mode_to_bpp(i);
			break;
		}
		return;

	case K_LEFTARROW:
		switch (vid_cursor)
		{
		case VID_FULLSCREEN:
			if (!num_fmodes)
				break;
			vid_menu_fs = !vid_menu_fs;
			vid_menunum = match_windowed_fullscr_modes();
			vid_menulist = (vid_menu_fs) ? (vmode_t *)fmodelist : (vmode_t *)wmodelist;
			vid_menubpp = vid_menulist[vid_menunum].bpp;
			break;
		case VID_RESOLUTION:
			S_LocalSound ("raven/menu1.wav");
			vid_menunum--;
			if (vid_menunum < 0 || vid_menubpp != vid_menulist[vid_menunum].bpp)
				vid_menunum++;
			break;
		case VID_BPP:
			i = find_bppnum (-1);
			if (vid_menubpp == bpplist[i][0])
				break;
			vid_menubpp = bpplist[i][0];
			//find a matching video mode for this bpp
			vid_menunum = match_mode_to_bpp(i);
			break;
		}
		return;

	default:
		break;
	}
}

