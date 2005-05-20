/*
	glwin32.h
	glquake header for win32/wgl

	$Id: glwin32.h,v 1.1 2005-05-20 18:16:45 sezero Exp $
*/


// disable data conversion warnings
#pragma warning(disable : 4244)     // MIPS
#pragma warning(disable : 4136)     // X86
#pragma warning(disable : 4051)     // ALPHA
  
#include <windows.h>

#include <gl\gl.h>
#include <gl\glu.h>

// Function prototypes for the Texture Object Extension routines
typedef GLboolean (APIENTRY *ARETEXRESFUNCPTR)(GLsizei, const GLuint *,
		   const GLboolean *);
typedef void (APIENTRY *BINDTEXFUNCPTR)(GLenum, GLuint);
typedef void (APIENTRY *DELTEXFUNCPTR)(GLsizei, const GLuint *);
typedef void (APIENTRY *GENTEXFUNCPTR)(GLsizei, GLuint *);
typedef GLboolean (APIENTRY *ISTEXFUNCPTR)(GLuint);
typedef void (APIENTRY *PRIORTEXFUNCPTR)(GLsizei, const GLuint *,
	      const GLclampf *);
typedef void (APIENTRY *TEXSUBIMAGEPTR)(int, int, int, int, int, int, int, int, void *);

extern	BINDTEXFUNCPTR bindTexFunc;
extern	DELTEXFUNCPTR delTexFunc;
extern	TEXSUBIMAGEPTR TexSubImage2DFunc;

// 3dfx functions
typedef int  (APIENTRY *FX_DISPLAY_MODE_EXT)(int);
typedef void (APIENTRY *FX_SET_PALETTE_EXT)( unsigned long * );
typedef void (APIENTRY *FX_MARK_PAL_TEXTURE_EXT)( void );
extern  FX_DISPLAY_MODE_EXT fxDisplayModeExtension;
extern  FX_SET_PALETTE_EXT fxSetPaletteExtension;
extern  FX_MARK_PAL_TEXTURE_EXT fxMarkPalTextureExtension;

