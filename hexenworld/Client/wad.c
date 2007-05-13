/*
	wad.c
	wad file loading

	$Id: wad.c,v 1.13 2007-05-13 11:59:02 sezero Exp $
*/

#include "quakedef.h"

int			wad_numlumps;
lumpinfo_t	*wad_lumps;
byte		*wad_base = NULL;

void SwapPic (qpic_t *pic);

/*
==================
W_CleanupName

Lowercases name and pads with spaces and a terminating 0 to the length of
lumpinfo_t->name.
Used so lumpname lookups can proceed rapidly by comparing 4 chars at a time
Space padding is so names can be printed nicely in tables.
Can safely be performed in place.
==================
*/
static void W_CleanupName (char *in, char *out)
{
	int		i;
	int		c;

	for (i = 0; i < 16; i++ )
	{
		c = in[i];
		if (!c)
			break;

		if (c >= 'A' && c <= 'Z')
			c += ('a' - 'A');
		out[i] = c;
	}

	for ( ; i < 16; i++ )
		out[i] = 0;
}


/*
====================
W_LoadWadFile
====================
*/
void W_LoadWadFile (const char *filename)
{
	lumpinfo_t		*lump_p;
	wadinfo_t		*header;
	int			i;
	int			infotableofs;

	if (wad_base)
		Z_Free (wad_base);
	wad_base = FS_LoadZoneFile (filename, Z_SECZONE);
//	if (!wad_base)
//		Sys_Error ("%s: couldn't load %s", __thisfunc__, filename);

	header = (wadinfo_t *)wad_base;

	if (header->identification[0] != 'W'
			|| header->identification[1] != 'A'
			|| header->identification[2] != 'D'
			|| header->identification[3] != '2')
		Sys_Error ("Wad file %s doesn't have WAD2 id\n",filename);

	wad_numlumps = LittleLong(header->numlumps);
	infotableofs = LittleLong(header->infotableofs);
	wad_lumps = (lumpinfo_t *)(wad_base + infotableofs);

	for (i = 0, lump_p = wad_lumps; i < wad_numlumps; i++, lump_p++)
	{
		lump_p->filepos = LittleLong(lump_p->filepos);
		lump_p->size = LittleLong(lump_p->size);
		W_CleanupName (lump_p->name, lump_p->name);
		if (lump_p->type == TYP_QPIC)
			SwapPic ( (qpic_t *)(wad_base + lump_p->filepos));
	}
}


/*
=============
W_GetLumpinfo
=============
*/
static lumpinfo_t *W_GetLumpinfo (char *name)
{
	int		i;
	lumpinfo_t	*lump_p;
	char	clean[16];

	W_CleanupName (name, clean);

	for (lump_p = wad_lumps, i = 0; i < wad_numlumps; i++, lump_p++)
	{
		if (!strcmp(clean, lump_p->name))
			return lump_p;
	}

	Sys_Error ("%s: %s not found", __thisfunc__, name);
	return NULL;
}

void *W_GetLumpName (char *name)
{
	lumpinfo_t	*lump;

	lump = W_GetLumpinfo (name);

	return (void *)(wad_base + lump->filepos);
}

#if 0	// no users
void *W_GetLumpNum (int num)
{
	lumpinfo_t	*lump;

	if (num < 0 || num > wad_numlumps)
		Sys_Error ("%s: bad number: %i", __thisfunc__, num);

	lump = wad_lumps + num;

	return (void *)(wad_base + lump->filepos);
}
#endif

/*
=============================================================================

automatic byte swapping

=============================================================================
*/

void SwapPic (qpic_t *pic)
{
	pic->width = LittleLong(pic->width);
	pic->height = LittleLong(pic->height);
}

