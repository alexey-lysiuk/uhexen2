/*
 * Unreal UMX container support.  UMX parsing is based on Unreal Media
 * Ripper (UMR) v0.3 by Andy Ward <wardwh@swbell.net>, with additional
 * updates by O. Sezer - see git repo at https://github.com/sezero/umr/
 *
 * $Id$
 *
 * Copyright (C) 2013 O.Sezer <sezero@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "quakedef.h"

#if defined(USE_CODEC_UMX)
#include "snd_codec.h"
#include "snd_codeci.h"
#include "snd_umx.h"

#define UPKG_HDR_TAG	0x9e2a83c1

#pragma pack(1)
struct upkg_hdr {
	uint32_t tag;	/* UPKG_HDR_TAG */
	int32_t file_version,	/* 61 for original unreal */
		pkg_flags,	/* bitflags - none needed */
		name_count,	/* number of names in name table (>= 0) */
		name_offset,		/* offset to name table  (>= 0) */
		export_count,	/* num. exports in export table  (>= 0) */
		export_offset,		/* offset to export table (>= 0) */
		import_count,	/* num. imports in export table  (>= 0) */
		import_offset;		/* offset to import table (>= 0) */

	/* number of GUIDs in heritage table (>= 1) and table's offset:
	 * only with versions < 68. */
	int32_t heritage_count, heritage_offset;
#if 0
	/* with versions >= 68:  a GUID, a dword for generation count
	 * and export_count and name_count dwords for each generation: */
	uint32_t guid[4];
	int32_t generation_count;
	struct _genhist {
		int32_t export_count,
			name_count;
	} genhist[0/* generation_count */];
#endif
};
#pragma pack()

/* read Little Endian data in an endian-neutral way */
#define READ_INT32(b) ((b)[0] | ((b)[1] << 8) | ((b)[2] << 16) | ((b)[3] << 24))

#define UMUSIC_IT	0
#define UMUSIC_S3M	1
#define UMUSIC_XM	2
#define UMUSIC_MOD	3
#define UMUSIC_WAV	4
#define UMUSIC_MP2	5

static const char *mustype[] = {
	"IT", "S3M", "XM", "MOD",
	"WAV", "MP2", NULL
};

/* decode an FCompactIndex. original documentation by Tim Sweeney
 * was at http://unreal.epicgames.com/Packages.htm
 */
static int32_t get_fci (const char *in, int *pos)
{
	int32_t a;
	int size;

	size = 1;
	a = in[0] & 0x3f;

	if (in[0] & 0x40) {
		size++;
		a |= (in[1] & 0x7f) << 6;

		if (in[1] & 0x80) {
			size++;
			a |= (in[2] & 0x7f) << 13;

			if (in[2] & 0x80) {
				size++;
				a |= (in[3] & 0x7f) << 20;

				if (in[3] & 0x80) {
					size++;
					a |= (in[4] & 0x3f) << 27;
				}
			}
		}
	}

	if (in[0] & 0x80)
		a = -a;

	*pos += size;

	return a;
}

static int get_objtype (fshandle_t *f, int32_t ofs, int type)
{
	char sig[16];
_retry:
	FS_fseek(f, ofs, SEEK_SET);
	FS_fread(sig, 16, 1, f);
	if (type == UMUSIC_IT) {
		if (memcmp(sig, "IMPM", 4) == 0)
			return UMUSIC_IT;
		return -1;
	}
	if (type == UMUSIC_XM) {
		if (memcmp(sig, "Extended Module:", 16) == 0)
			return UMUSIC_XM;
		return -1;
	}
	if (type == UMUSIC_MP2) {
		unsigned char *p = (unsigned char *)sig;
		uint16_t u = ((p[0] << 8) | p[1]) & 0xFFFE;
		if (u == 0xFFFC || u == 0xFFF4)
			return UMUSIC_MP2;
		return -1;
	}
	if (type == UMUSIC_WAV) {
		if (memcmp(sig, "RIFF", 4) == 0 && memcmp(&sig[8], "WAVE", 4) == 0)
			return UMUSIC_WAV;
		return -1;
	}

	FS_fseek(f, ofs + 44, SEEK_SET);
	FS_fread(sig, 4, 1, f);
	if (type == UMUSIC_S3M) {
		if (memcmp(sig, "SCRM", 4) == 0)
			return UMUSIC_S3M;
		/*return -1;*/
		/* SpaceMarines.umx and Starseek.umx from Return to NaPali
		 * report as "s3m" whereas the actual music format is "it" */
		type = UMUSIC_IT;
		goto _retry;
	}

	FS_fseek(f, ofs + 1080, SEEK_SET);
	FS_fread(sig, 4, 1, f);
	if (type == UMUSIC_MOD) {
		if (memcmp(sig, "M.K.", 4) == 0 || memcmp(sig, "M!K!", 4) == 0)
			return UMUSIC_MOD;
		return -1;
	}

	return -1;
}

static int read_export (fshandle_t *f, const struct upkg_hdr *hdr,
			int32_t *ofs, int32_t *objsize)
{
	char buf[40];
	int idx = 0, t;

	FS_fseek(f, *ofs, SEEK_SET);
	if (FS_fread(buf, 4, 10, f) < 10)
		return -1;

	get_fci(&buf[idx], &idx);		/* skip junk */
	t = get_fci(&buf[idx], &idx);		/* type_name */
	if (hdr->file_version > 61) idx += 4;	/* skip export size */
	*objsize = get_fci(&buf[idx], &idx);
	*ofs += idx;	/* offset for real data */

	return t;	/* return type_name index */
}

static int read_typname(fshandle_t *f, const struct upkg_hdr *hdr,
			int idx, char *out)
{
	int i, s;
	long l;
	char buf[64];

	if (idx >= hdr->name_count) return -1;
	for (i = 0, l = 0; i <= idx; i++) {
		FS_fseek(f, hdr->name_offset + l, SEEK_SET);
		FS_fread(buf, 1, 64, f);
		if (hdr->file_version >= 64) {
			s = *(signed char *)buf; /* numchars *including* terminator */
			if (s <= 0 || s >= 64) return -1;
			l += s + 5;	/* 1 for buf[0], 4 for int32_t name_flags */
		} else {
			buf[63] = 0;
			l += (long)strlen(buf);
			l +=  5;	/* 1 for terminator, 4 for int32_t name_flags */
		}
	}

	strcpy(out, (hdr->file_version >= 64)? &buf[1] : buf);
	return 0;
}

static int probe_umx   (fshandle_t *f, const struct upkg_hdr *hdr,
			int32_t *ofs, int32_t *objsize)
{
	int i, idx, t;
	int32_t s, pos;
	long fsiz;
	char buf[64];

	idx = 0;
	fsiz = f->length;

	/* Find the offset and size of the first IT, S3M or XM
	 * by parsing the exports table. The umx files should
	 * have only one export. Kran32.umx from Unreal has two,
	 * but both pointing to the same music. */
	if (hdr->export_offset >= fsiz) return -1;
	memset(buf, 0, 64);
	FS_fseek(f, hdr->export_offset, SEEK_SET);
	FS_fread(buf, 1, 64, f);

	get_fci(&buf[idx], &idx);	/* skip class_index */
	idx += 4;			/* skip int32 package_index */
	get_fci(&buf[idx], &idx);	/* skip super_index */
	get_fci(&buf[idx], &idx);	/* skip object_name */
	idx += 4;			/* skip int32 object_flags */

	s = get_fci(&buf[idx], &idx);	/* get serial_size */
	if (s <= 0) return -1;
	pos = get_fci(&buf[idx],&idx);	/* get serial_offset */
	if (pos < 0 || pos > fsiz - 40) return -1;

	if ((t = read_export(f, hdr, &pos, &s)) < 0) return -1;
	if (s <= 0 || s > fsiz - pos) return -1;

	if (read_typname(f, hdr, t, buf) < 0) return -1;
	for (i = 0; mustype[i] != NULL; i++) {
		if (!q_strcasecmp(buf, mustype[i])) {
			t = i;
			break;
		}
	}
	if (mustype[i] == NULL) return -1;
	if ((t = get_objtype(f, pos, t)) < 0) return -1;

	*ofs = pos;
	*objsize = s;
	return t;
}

static int32_t probe_header (void *header)
{
	struct upkg_hdr *hdr;
	unsigned char *p;
	uint32_t *swp;
	int i;

	/* byte swap the header - all members are 32 bit LE values */
	p = (unsigned char *) header;
	swp = (uint32_t *) header;
	for (i = 0; i < (int)sizeof(struct upkg_hdr)/4; i++, p += 4) {
		swp[i] = READ_INT32(p);
	}

	hdr = (struct upkg_hdr *) header;
	if (hdr->tag != UPKG_HDR_TAG) {
		Con_DPrintf("Unknown header tag 0x%x\n", hdr->tag);
		return -1;
	}
	if (hdr->name_count	< 0	||
	    hdr->name_offset	< 0	||
	    hdr->export_count	< 0	||
	    hdr->export_offset	< 0	||
	    hdr->import_count	< 0	||
	    hdr->import_offset	< 0	) {
		Con_DPrintf("Negative values in header\n");
		return -1;
	}
	switch (hdr->file_version) {
	case 61:/* Unreal */
	case 62:/* Unreal Tournament */
	case 63:/* Return to NaPali */
	case 64:/* Unreal Tournament */
	case 66:/* Unreal Tournament */
	case 68:/* Unreal Tournament */
	case 69:/* Tactical Ops */
	case 75:/* Harry Potter and the Philosopher's Stone */
	case 76:			/* mpeg layer II data */
	case 83:/* Mobile Forces */
		return 0;
	}

	Con_DPrintf("Unknown upkg version %d\n", hdr->file_version);
	return -1;
}

static int process_upkg (fshandle_t *f, int32_t *ofs, int32_t *objsize)
{
	char header[64];

	if (FS_fread(header, 1, 64, f) < 64)
		return -1;
	if (probe_header(header) < 0)
		return -1;

	return probe_umx(f, (struct upkg_hdr *)header, ofs, objsize);
}

static qboolean S_UMX_CodecInitialize (void)
{
	return true;
}

static void S_UMX_CodecShutdown (void)
{
}

static qboolean S_UMX_CodecOpenStream (snd_stream_t *stream)
{
	int32_t ofs, size;
	int type;

	type = process_upkg(&stream->fh, &ofs, &size);
	if (type < 0) {
		Con_DPrintf("%s: unrecognized umx\n", stream->name);
		return false;
	}

	Con_DPrintf("%s: %s data @ 0x%x, %d bytes\n", stream->name, mustype[type], ofs, size);
	/* hack the fshandle_t start pos and length members so
	 * that only the relevant data is accessed from now on */
	stream->fh.start = ofs;
	stream->fh.length = size;
	FS_fseek(&stream->fh, 0, SEEK_SET);

	switch (type) {
	case UMUSIC_IT:
	case UMUSIC_S3M:
	case UMUSIC_XM:
	case UMUSIC_MOD: return S_CodecForwardStream(stream, CODECTYPE_MOD);
	case UMUSIC_WAV: return S_CodecForwardStream(stream, CODECTYPE_WAV);
	case UMUSIC_MP2: return S_CodecForwardStream(stream, CODECTYPE_MP3);
	}

	return false;
}

static int S_UMX_CodecReadStream (snd_stream_t *stream, int bytes, void *buffer)
{
	return -1;
}

static void S_UMX_CodecCloseStream (snd_stream_t *stream)
{
	S_CodecUtilClose(&stream);
}

static int S_UMX_CodecRewindStream (snd_stream_t *stream)
{
	return -1;
}

snd_codec_t umx_codec =
{
	CODECTYPE_UMX,
	true,	/* always available. */
	"umx",
	S_UMX_CodecInitialize,
	S_UMX_CodecShutdown,
	S_UMX_CodecOpenStream,
	S_UMX_CodecReadStream,
	S_UMX_CodecRewindStream,
	S_UMX_CodecCloseStream,
	NULL
};

#endif	/* USE_CODEC_UMX */

