/*
	gl_opt.h
	opengl compile-time options

	$Id: gl_opt.h,v 1.2 2006-04-10 12:48:44 sezero Exp $
*/


// COMPILE TIME OPTION: DO_MESH_CACHE
// if you want to cache the alias model meshes to disk, the define below must
// be 1, otherwise 0. caching gives bad results upon the following conditions:
// - you change the mdl file with an override file, but you have a cached ms2
//   file in the glhexen directory from the old mdl,
// - you run a mod without the mission pack first and then with mission pack,
//   or vice versa, and the ms2 cache from the previous instance would mismatch
//   the mdl from the latter (in fact, this situation can be overcome.)
#define	DO_MESH_CACHE		0

// COMPILE TIME OPTION: USE_HEXEN2_PALTEX_CODE
// to use hexenworld (quake)'s palettized texture code instead of the original
// hexen2 code, change the definition below to 0.  that will result in lower
// quality. see gl_draw.c (and gl_vidXXX.c) for more details
#define	USE_HEXEN2_PALTEX_CODE		1

// COMPILE TIME OPTION: USE_HEXEN2_RESAMPLER_CODE
// to use hexenworld (quake)'s texture resampler code instead of the original
// hexen2 code, change the definition below to 0. that will result in slightly
// sharper but "jaggier" textures here and there. see in gl_draw.c. bad results
// especially detectable on help screens when using old cards with small (256k)
// texture size support (read: old voodoo boards.)
#define	USE_HEXEN2_RESAMPLER_CODE	1

// COMPILE TIME OPTION: GL_DLSYM
// This optioh has to be edited in the Makefile (see the LINK_GL_LIBS option in
// there), not here. It affects the final linkage of the binary.

// COMPILE TIME OPTION: ENABLE_SCRAP
// scrap allocation is disabled by default. it doesn't work good with vid_mode
// changes. if you want to enable it, change the define below to 1.
#define ENABLE_SCRAP		0

// COMPILE TIME OPTION: GLTEST
// the macro GLTEST enables some experimentation stuff for developers. If you
// want to enable it, un-comment the line below. This option isn't for users.
//#define	GLTEST			// experimental stuff

