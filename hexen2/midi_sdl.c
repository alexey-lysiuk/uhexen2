/*
	midi_sdl.c
	midiplay via SDL_mixer

	$Id: midi_sdl.c,v 1.28 2006-10-10 07:24:24 sezero Exp $
*/

#include "quakedef.h"
#include <unistd.h>
#include <dlfcn.h>
#include "sdl_inc.h"
#define _SND_SYS_MACROS_ONLY
#include "snd_sys.h"

static Mix_Music *music = NULL;
static int audio_wasinit = 0;

static qboolean bMidiInited, bFileOpen, bPlaying, bPaused, bLooped;
extern cvar_t bgmvolume;
static float bgm_volume_old = -1.0f;

static void (*midi_endmusicfnc)(void);

static void MIDI_Play_f (void)
{
	if (Cmd_Argc () == 2)
	{
		MIDI_Play(Cmd_Argv(1));
	}
}

static void MIDI_Stop_f (void)
{
	MIDI_Stop();
}

static void MIDI_Pause_f (void)
{
	MIDI_Pause (MIDI_TOGGLE_PAUSE);
}

static void MIDI_Loop_f (void)
{
	if (Cmd_Argc () == 2)
	{
		if (Q_strcasecmp(Cmd_Argv(1),"on") == 0 || Q_strcasecmp(Cmd_Argv(1),"1") == 0)
			MIDI_Loop(1);
		else if (Q_strcasecmp(Cmd_Argv(1),"off") == 0 || Q_strcasecmp(Cmd_Argv(1),"0") == 0)
			MIDI_Loop(0);
		else if (Q_strcasecmp(Cmd_Argv(1),"toggle") == 0)
			MIDI_Loop(2);
	}

	if (bLooped)
		Con_Printf("MIDI music will be looped\n");
	else
		Con_Printf("MIDI music will not be looped\n");
}

static void MIDI_SetVolume(float volume_frac)
{
	if (!bMidiInited)
		return;

	volume_frac = (volume_frac >= 0.0f) ? volume_frac : 0.0f;
	volume_frac = (volume_frac <= 1.0f) ? volume_frac : 1.0f;
	Mix_VolumeMusic(volume_frac*128); /* needs to be between 0 and 128 */
}

void MIDI_Update(void)
{
	if (bgmvolume.value != bgm_volume_old)
	{
		bgm_volume_old = bgmvolume.value;
		MIDI_SetVolume(bgm_volume_old);
	}
}

static void MIDI_EndMusicFinished(void)
{
	Sys_Printf("Music finished\n");
	if (bLooped)
	{
		Sys_Printf("Looping enabled\n");
		if (Mix_PlayingMusic())
			Mix_HaltMusic();

		Sys_Printf("Playing again\n");
		Mix_RewindMusic();
		Mix_FadeInMusic(music,0,2000);
		bPlaying = 1;
	}
}

qboolean MIDI_Init(void)
{
#warning FIXME: midi doesnt play with snd_sdl.c
	int audio_rate = 22050;
	int audio_format = AUDIO_S16;
	int audio_channels = 2;
	int audio_buffers = 4096;
	char	mididir[MAX_OSPATH];
	void	*selfsyms;
	const SDL_version *smixer_version;
	SDL_version *(*Mix_Linked_Version_fp)(void) = NULL;

	bMidiInited = 0;
	Con_Printf("MIDI_Init: ");

	if (COM_CheckParm("-nomidi") || COM_CheckParm("--nomidi")
	   || COM_CheckParm("-nosound") || COM_CheckParm("--nosound"))
	{
		Con_Printf("disabled by commandline\n");
		return 0;
	}

	if (snd_system == S_SYS_SDL)
	{
		Con_Printf("SDL_mixer conflicts SDL audio.\n");
		return 0;
	}

	Con_Printf("SDL_Mixer ");
	// this is to avoid relocation errors with very old SDL_Mixer versions
	selfsyms = dlopen(NULL, RTLD_LAZY);
	if (selfsyms != NULL)
	{
		Mix_Linked_Version_fp = dlsym(selfsyms, "Mix_Linked_Version");
		dlclose(selfsyms);
	}
	if (Mix_Linked_Version_fp == NULL)
	{
		Con_Printf("version can't be determined, disabled.\n");
		goto bad_version;
	}

	smixer_version = Mix_Linked_Version();
	Con_Printf("v%d.%d.%d is ",smixer_version->major,smixer_version->minor,smixer_version->patch);
	// reject running with SDL_Mixer versions older than what is stated in sdl_inc.h
	if (SDL_VERSIONNUM(smixer_version->major,smixer_version->minor,smixer_version->patch) < MIX_REQUIREDVERSION)
	{
		Con_Printf("too old, disabled.\n");
bad_version:
		Con_Printf("You need at least v%d.%d.%d of SDL_Mixer\n",SDL_MIXER_MIN_X,SDL_MIXER_MIN_Y,SDL_MIXER_MIN_Z);
		bMidiInited = 0;
		return 0;
	}
	Con_Printf("found.\n");

	Q_snprintf_err(mididir, sizeof(mididir), "%s/.midi", com_userdir);
	Sys_mkdir_err (mididir);

	// Try initing the audio subsys if it hasn't been already
	audio_wasinit = SDL_WasInit(SDL_INIT_AUDIO);
	if (audio_wasinit == 0)
	{
		if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
		{
			Con_Printf("MIDI_Init: Cannot initialize SDL_AUDIO: %s\n",SDL_GetError());
			bMidiInited = 0;
			return 0;
		}
	}

	if (Mix_OpenAudio(audio_rate, audio_format, audio_channels, audio_buffers) < 0)
	{
		bMidiInited = 0;
		Con_Printf("SDL_mixer: open audio failed: %s\n", SDL_GetError());
		if (audio_wasinit == 0)
			SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return 0;
	}

	midi_endmusicfnc = &MIDI_EndMusicFinished;
	if (midi_endmusicfnc)
		Mix_HookMusicFinished(midi_endmusicfnc);

	Con_Printf("MIDI music initialized.\n");

	Cmd_AddCommand ("midi_play", MIDI_Play_f);
	Cmd_AddCommand ("midi_stop", MIDI_Stop_f);
	Cmd_AddCommand ("midi_pause", MIDI_Pause_f);
	Cmd_AddCommand ("midi_loop", MIDI_Loop_f);

	bFileOpen = 0;
	bPlaying = 0;
	bLooped = 1;
	bPaused = 0;
	bMidiInited = 1;

	return true;
}

void MIDI_Play(char *Name)
{
	void	*midiData;
	char	midiName[MAX_OSPATH];

	if (!bMidiInited)	//don't try to play if there is no midi
		return;

	MIDI_Stop();

	if (!Name || !*Name)
	{
		Sys_Printf("no midi music to play\n");
		return;
	}

	// Note that midi/ is the standart quake search path, but
	// .midi/ with the leading dot is the path in the userdir
	snprintf (midiName, sizeof(midiName), "%s/.midi/%s.mid", com_userdir, Name);
	if (access(midiName, R_OK) != 0)
	{
		Sys_Printf("Extracting file midi/%s.mid\n", Name);
		midiData = COM_LoadHunkFile (va("midi/%s.mid", Name));
		if (!midiData)
		{
			Con_Printf("musicfile midi/%s.mid not found\n", Name);
			return;
		}
		if (COM_WriteFile (va(".midi/%s.mid", Name), midiData, com_filesize))
			return;
	}

	music = Mix_LoadMUS(midiName);
	if ( music == NULL )
	{
		Sys_Printf("Couldn't load %s: %s\n", midiName, SDL_GetError());
	}
	else
	{
		bFileOpen = 1;
		Con_Printf ("Started midi music %s\n", Name);
		Mix_FadeInMusic(music,0,2000);
		bPlaying = 1;
	}
}

void MIDI_Pause(int mode)
{
	if (!bPlaying)
		return;

	if ((mode == MIDI_TOGGLE_PAUSE && bPaused) || mode == MIDI_ALWAYS_RESUME)
	{
		Mix_ResumeMusic();
		bPaused = false;
	}
	else
	{
		Mix_PauseMusic();
		bPaused = true;
	}
}

void MIDI_Loop(int NewValue)
{
	Sys_Printf("MIDI_Loop\n");
	if (NewValue == 2)
		bLooped = !bLooped;
	else
		bLooped = NewValue;

	MIDI_EndMusicFinished();
}

void MIDI_Stop(void)
{
	if (!bMidiInited)	//Just to be safe
		return;

	if(bFileOpen || bPlaying)
	{
		Mix_HaltMusic();
		Mix_FreeMusic(music);
	}

	bPlaying = bPaused = 0;
	bFileOpen=0;
}

void MIDI_Cleanup(void)
{
	if (bMidiInited)
	{
		MIDI_Stop();
		bMidiInited = 0;
		Con_Printf("MIDI_Cleanup: closing SDL_mixer\n");
		Mix_CloseAudio();
	//	if (audio_wasinit == 0)
	//		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	}
}

/*
 * $Log: not supported by cvs2svn $
 * Revision 1.27  2006/10/06 16:43:32  sezero
 * updated the makefiles and preprocessor definitions:
 * * moved the HAVE_GCC_4_0 definition/detection from the include makefiles
 *   to the actual makefiles
 * * building on windows now requires msys. removed the half-baked support
 *   for absences a unixish shell.
 * * used a unified ifeq syntax in all of the Makefiles.
 * * obsoleted _NEED_SDL_MIXER in favor of a new _MIDI_SDLMIXER definition
 * * updated sdl_inc.h and made it always include SDL_mixer.h according to
 *   the _MIDI_SDLMIXER definition. updated sdl version requirements.
 * * removed the pointless NO_MIDIMUSIC definitions from Xcode files.
 *
 * Revision 1.26  2006/09/27 17:17:30  sezero
 * a lot of clean-ups in sound and midi files.
 *
 * Revision 1.25  2006/09/15 21:43:31  sezero
 * use snprintf and the strl* functions, #10: midi_mac.c and midi_sdl.c.
 * also did some clean-ups while we were there.
 *
 * Revision 1.24  2006/09/11 11:21:17  sezero
 * added human readable defines for the MIDI_Pause modes
 *
 * Revision 1.23  2006/06/29 23:02:02  sezero
 * cleaned up some things in the build system. added no sound and
 * no cdaudio options. removed static build targets from hexen2
 * and hexenworld makefiles. misc small things.
 *
 * Revision 1.22  2006/05/18 17:48:10  sezero
 * renamed MIDI_UpdateVolume to MIDI_Update
 *
 * Revision 1.21  2006/05/18 17:47:01  sezero
 * set bMidiInited to 0 in MIDI_Cleanup()
 *
 * Revision 1.20  2006/05/18 17:46:10  sezero
 * made COM_WriteFile() to return 0 on success, 1 on write errors
 *
 * Revision 1.19  2006/02/18 13:44:14  sezero
 * continue making static functions and vars static. whitespace and coding style
 * cleanup. (part 6: midi files).
 *
 * Revision 1.18  2006/01/12 12:43:49  sezero
 * Created an sdl_inc.h with all sdl version requirements and replaced all
 * SDL.h and SDL_mixer.h includes with it. Made the source to compile against
 * SDL versions older than 1.2.6 without disabling multisampling. Multisampling
 * (fsaa) option is now decided at runtime. Minimum required SDL and SDL_mixer
 * versions are now 1.2.4. If compiled without midi, minimum SDL required is
 * 1.2.0. Added SDL_mixer version checking to sdl-midi with measures to prevent
 * relocation errors.
 *
 * Revision 1.17  2005/11/23 18:05:51  sezero
 * changed USE_MIDI definition usage with a NO_MIDIMUSIC definition.
 * associated makefile changes will follow.
 *
 * Revision 1.16  2005/11/02 18:39:21  sezero
 * shortened midi file opening for SDL_mixer. the midi cache
 * will be directly under <com_userdir>/.midi from now on.
 *
 * Revision 1.15  2005/08/13 11:57:45  sezero
 * Standardized SDL_mixer includes
 *
 * Revision 1.14  2005/08/12 08:12:51  sezero
 * updated sdl includes of midi for freebsd (from Steven)
 *
 * Revision 1.13  2005/07/23 19:49:55  sezero
 * better handling of midi file size
 *
 * Revision 1.12  2005/07/23 11:34:51  sezero
 * fixed midi not playing when midifile exists in
 * the searchpath but not in com_userdir/.midi .
 *
 * Revision 1.11  2005/05/19 12:46:56  sezero
 * synced h2 and hw versions of midi stuff
 *
 * Revision 1.10  2005/05/17 22:56:19  sezero
 * cleanup the "stricmp, strcmpi, strnicmp, Q_strcasecmp, Q_strncasecmp" mess:
 * Q_strXcasecmp will now be used throughout the code which are implementation
 * dependant defines for __GNUC__ (strXcasecmp) and _WIN32 (strXicmp)
 *
 * Revision 1.9  2005/04/30 08:13:43  sezero
 * wrong casts in midi_sdl.c
 *
 * Revision 1.8  2005/04/14 07:35:27  sezero
 * no need to announce MIDI_Cleanup if we'll never do it..
 *
 * Revision 1.7  2005/03/06 10:44:41  sezero
 * - move reinit_music to menu.c where it belongs
 * - fix reinit_music so that it works for the F4 key as well
 * - don't mess with music volume on every frame update, it's just silly
 *
 * Revision 1.6  2005/02/05 16:30:14  sezero
 * don't try extracting anything if no midi file is given
 *
 * Revision 1.5  2005/02/05 16:21:13  sezero
 * killed Com_LoadHunkFile2()  [from HexenWorld]
 *
 * Revision 1.4  2005/02/05 16:20:14  sezero
 * fix possible path length overflows
 *
 * Revision 1.3  2005/02/05 16:18:25  sezero
 * added midi volume control (partially from Pa3PyX)
 *
 * Revision 1.2  2005/02/05 16:17:29  sezero
 * - Midi file paths cleanup. these should be leftovers
 *   from times when gamedir and userdir were the same.
 * - Killed Com_WriteFileFullPath(), not used anymore.
 * - Replaced some Con_Printf() with Sys_Printf().
 *
 * Revision 1.1  2005/02/05 16:16:06  sezero
 * separate win32 and linux versions of midi files. too much mess otherwise.
 *
 *
 * 2005/02/04 14:00:14  sezero
 * - merge small bits from the hexenworld version
 * - rest of the cleanup
 *
 * 2005/02/04 13:51:31  sezero
 * midi fixes for correctness' sake. it still fails with snd_sdl.
 *
 * 2005/02/04 11:28:59  sezero
 * make sdl_audio actually work (finally)
 *
 * 2004/12/18 13:20:37  sezero
 * make the music automatically restart when changed in the options
 * menu (stevena)
 *
 * 2002/01/04 13:50:06  phneutre
 * music looping fix
 *
 * 2002/01/02 14:25:59  phneutre
 * force -nomidi if -nosound is set
 *
 * 2002/01/01 00:55:21  phneutre
 * fixed compilation problem when -DUSEMIDI is not set
 *
 * 2001/12/15 14:35:27  phneutre
 * more midi stuff (loop and volume)
 *
 * 2001/12/13 17:28:38  phneutre
 * use ~/.aot/ instead of data1/ to create meshes or extract temp files
 *
 * 2001/12/13 16:48:55  phneutre
 * create glhexen/midi subdir to copy .mid files
 *
 * 2001/12/11 19:17:50  phneutre
 * initial support for MIDI music (requieres SDL_mixer, see Makefile
 *
 */
