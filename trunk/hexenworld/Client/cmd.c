/*
	cmd.c
	Quake script command processing module

	$Header: /home/ozzie/Download/0000/uhexen2/hexenworld/Client/cmd.c,v 1.17 2007-02-02 14:21:31 sezero Exp $
*/

#include "quakedef.h"

#define	MAX_ALIAS_NAME	32
#define	MAX_ARGS	80

cmd_source_t	cmd_source;

typedef struct cmdalias_s
{
	struct cmdalias_s	*next;
	char	name[MAX_ALIAS_NAME];
	char	*value;
} cmdalias_t;

typedef struct cmd_function_s
{
	struct cmd_function_s	*next;
	char					*name;
	xcommand_t				function;
} cmd_function_t;

static cmdalias_t	*cmd_alias = NULL;
static cmd_function_t	*cmd_functions = NULL;

static	int			cmd_argc;
static	char		*cmd_argv[MAX_ARGS];
static	char		*cmd_null_string = "";
static	char		*cmd_args = NULL;

static qboolean	cmd_wait;

cvar_t cl_warncmd = {"cl_warncmd", "0", CVAR_NONE};


//=============================================================================

/*
============
Cmd_Wait_f

Causes execution of the remainder of the command buffer to be delayed until
next frame.  This allows commands like:
bind g "impulse 5 ; +attack ; wait ; -attack ; impulse 2"
============
*/
static void Cmd_Wait_f (void)
{
	cmd_wait = true;
}

/*
=============================================================================

						COMMAND BUFFER

=============================================================================
*/

static sizebuf_t	cmd_text;
static byte	cmd_text_buf[8192];

/*
============
Cbuf_Init
============
*/
void Cbuf_Init (void)
{
	// space for commands and script files
	SZ_Init (&cmd_text, cmd_text_buf, sizeof(cmd_text_buf));
}


/*
============
Cbuf_AddText

Adds command text at the end of the buffer
============
*/
void Cbuf_AddText (char *text)
{
	int		l;

	l = strlen (text);

	if (cmd_text.cursize + l >= cmd_text.maxsize)
	{
		Con_Printf ("Cbuf_AddText: overflow\n");
		return;
	}
	SZ_Write (&cmd_text, text, l);
}


/*
============
Cbuf_InsertText

Adds command text immediately after the current command
Adds a \n to the text
FIXME: actually change the command buffer to do less copying
============
*/
void Cbuf_InsertText (char *text)
{
	char	*temp;
	int		templen;

// copy off any commands still remaining in the exec buffer
	templen = cmd_text.cursize;
	if (templen)
	{
		temp = Z_Malloc (templen);
		memcpy (temp, cmd_text.data, templen);
		SZ_Clear (&cmd_text);
	}
	else
		temp = NULL;	// shut up compiler

// add the entire text of the file
	Cbuf_AddText (text);
	SZ_Write (&cmd_text, "\n", 1);
// add the copied off data
	if (templen)
	{
		SZ_Write (&cmd_text, temp, templen);
		Z_Free (temp);
	}
}

/*
============
Cbuf_Execute
============
*/
void Cbuf_Execute (void)
{
	int		i;
	char	*text;
	char	line[1024];
	int		quotes;

	while (cmd_text.cursize)
	{
// find a \n or ; line break
		text = (char *)cmd_text.data;

		quotes = 0;
		for (i = 0; i < cmd_text.cursize; i++)
		{
			if (text[i] == '"')
				quotes++;
			if ( !(quotes&1) &&  text[i] == ';')
				break;	// don't break if inside a quoted string
			if (text[i] == '\n')
				break;
		}

		memcpy (line, text, i);
		line[i] = 0;

// delete the text from the command buffer and move remaining commands down
// this is necessary because commands (exec, alias) can insert data at the
// beginning of the text buffer

		if (i == cmd_text.cursize)
			cmd_text.cursize = 0;
		else
		{
			i++;
			cmd_text.cursize -= i;
			memcpy (text, text+i, cmd_text.cursize);
		}

// execute the command line
		Cmd_ExecuteString (line, src_command);

		if (cmd_wait)
		{	// skip out while text still remains in buffer, leaving it
			// for next frame
			cmd_wait = false;
			break;
		}
	}
}

/*
==============================================================================

						SCRIPT COMMANDS

==============================================================================
*/

/*
===============
Cmd_StuffCmds_f

Adds command line parameters as script statements
Commands lead with a +, and continue until a - or another +
quake +prog jctest.qp +cmd amlev1
quake -nosound +cmd amlev1
===============
*/
void Cmd_StuffCmds_f (void)
{
	int		i, j;
	int		s;
	char	*text, *build, c;

// build the combined string to parse from
	s = 0;
	for (i = 1; i < com_argc; i++)
	{
		if (!com_argv[i])
			continue;		// NEXTSTEP nulls out -NXHost
		s += strlen (com_argv[i]) + 1;
	}
	if (!s)
		return;

	text = Z_Malloc (s+1);
	text[0] = 0;
	for (i = 1; i < com_argc; i++)
	{
		if (!com_argv[i])
			continue;		// NEXTSTEP nulls out -NXHost
		strcat (text,com_argv[i]);
		if (i != com_argc-1)
			strcat (text, " ");
	}

// pull out the commands
	build = Z_Malloc (s+1);
	build[0] = 0;

	for (i = 0; i < s-1; i++)
	{
		if (text[i] == '+')
		{
			i++;

			for (j = i; (text[j] != '+') && (text[j] != '-') && (text[j] != 0); j++)
				;

			c = text[j];
			text[j] = 0;

			strcat (build, text+i);
			strcat (build, "\n");
			text[j] = c;
			i = j-1;
		}
	}

	if (build[0])
		Cbuf_InsertText (build);

	Z_Free (text);
	Z_Free (build);
}


/*
===============
Cmd_Exec_f
===============
*/
static void Cmd_Exec_f (void)
{
	char	*f;
	int		mark;

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("exec <filename> : execute a script file\n");
		return;
	}

	// FIXME: is this safe freeing the hunk here???
	mark = Hunk_LowMark ();
	f = (char *)COM_LoadHunkFile (Cmd_Argv(1));
	if (!f)
	{
		Con_Printf ("couldn't exec %s\n",Cmd_Argv(1));
		return;
	}
	if (!Cvar_Command () && (cl_warncmd.value || developer.value))
		Con_Printf ("execing %s\n",Cmd_Argv(1));

	Cbuf_InsertText (f);
	Hunk_FreeToLowMark (mark);
}


/*
===============
Cmd_Echo_f

Just prints the rest of the line to the console
===============
*/
static void Cmd_Echo_f (void)
{
	int		i;

	for (i = 1; i < Cmd_Argc(); i++)
		Con_Printf ("%s ", Cmd_Argv(i));
	Con_Printf ("\n");
}

/*
===============
Cmd_Alias_f

Creates a new command that executes a command string (possibly ; seperated)
===============
*/
static void Cmd_Alias_f (void)
{
	cmdalias_t	*a;
	char		cmd[1024];
	int			i, c;
	char		*s;

	if (Cmd_Argc() == 1)
	{
		Con_Printf ("Current alias commands:\n");
		for (a = cmd_alias ; a ; a = a->next)
			Con_Printf ("%s : %s\n", a->name, a->value);
		return;
	}

	s = Cmd_Argv(1);

	if (Cmd_Argc() == 2)
	{
		for (a = cmd_alias ; a ; a = a->next)
		{
			if ( !strcmp(s, a->name) )
			{
				Con_Printf ("\"%s\" is \"%s\"\n", s, a->value);
				return;
			}
		}
		Con_Printf ("No alias named %s\n", s);
		return;
	}

	if (strlen(s) >= MAX_ALIAS_NAME)
	{
		Con_Printf ("Alias name is too long\n");
		return;
	}

	// if the alias already exists, reuse it
	for (a = cmd_alias ; a ; a = a->next)
	{
		if ( !strcmp(s, a->name) )
		{
			Z_Free (a->value);
			break;
		}
	}

	if (!a)
	{
		a = Z_Malloc (sizeof(cmdalias_t));
		a->next = cmd_alias;
		cmd_alias = a;
	}
	strcpy (a->name, s);

// copy the rest of the command line
	cmd[0] = 0;		// start out with a null string
	c = Cmd_Argc();
	for (i = 2; i < c; i++)
	{
		Q_strlcat (cmd, Cmd_Argv(i), sizeof(cmd));
		if (i != c-1)
			strcat (cmd, " ");
	}
	if (Q_strlcat(cmd, "\n", sizeof(cmd)) >= sizeof(cmd))
	{
		Con_Printf("alias value too long!\n");
		cmd[0] = '\n';	// nullify the string
		cmd[1] = 0;
	}

	a->value = Z_Malloc (strlen (cmd) + 1);
	strcpy (a->value, cmd);
}

/*
===============
Cmd_Unalias_f

Delete an alias
===============
*/
void Cmd_Unalias_f (void)
{
	cmdalias_t	*prev = NULL, *a;

	if (Cmd_Argc() != 2)
	{
		Con_Printf("unalias <name> : delete alias\n"
			   "unaliasall : delete all aliases\n");
		return;
	}

	for (a = cmd_alias ; a ; a=a->next)
	{
		if ( !strcmp(Cmd_Argv(1), a->name) )
		{
			if (prev)
				prev->next = a->next;
			else
				cmd_alias  = a->next;

			Z_Free (a->value);
			Z_Free (a);
			return;
		}
		prev = a;
	}

	Con_Printf ("No alias named %s\n", Cmd_Argv(1));
}

/*
===============
Cmd_Unaliasall_f

Delete all aliases
===============
*/
void Cmd_Unaliasall_f (void)
{
	cmdalias_t	*a;

	while (cmd_alias)
	{
		a = cmd_alias->next;
		Z_Free(cmd_alias->value);
		Z_Free(cmd_alias);
		cmd_alias = a;
	}
}

/*
=============================================================================

					COMMAND EXECUTION

=============================================================================
*/

/*
============
Cmd_Argc
============
*/
int Cmd_Argc (void)
{
	return cmd_argc;
}

/*
============
Cmd_Argv
============
*/
char *Cmd_Argv (int arg)
{
	if ( (unsigned)arg >= cmd_argc )
		return cmd_null_string;
	return cmd_argv[arg];
}

/*
============
Cmd_Args

Returns a single string containing argv(1) to argv(argc()-1)
============
*/
char *Cmd_Args (void)
{
	if (!cmd_args)
		return "";
	return cmd_args;
}


/*
============
Cmd_TokenizeString

Parses the given string into command line tokens.
============
*/
void Cmd_TokenizeString (char *text)
{
	int		i;

// clear the args from the last string
	for (i = 0; i < cmd_argc; i++)
		Z_Free (cmd_argv[i]);

	cmd_argc = 0;
	cmd_args = NULL;

	while (1)
	{
// skip whitespace up to a /n
		while (*text && *text <= ' ' && *text != '\n')
		{
			text++;
		}

		if (*text == '\n')
		{	// a newline seperates commands in the buffer
			text++;
			break;
		}

		if (!*text)
			return;

		if (cmd_argc == 1)
			cmd_args = text;

		text = COM_Parse (text);
		if (!text)
			return;

		if (cmd_argc < MAX_ARGS)
		{
			cmd_argv[cmd_argc] = Z_Malloc (strlen(com_token)+1);
			strcpy (cmd_argv[cmd_argc], com_token);
			cmd_argc++;
		}
	}
}


/*
============
Cmd_AddCommand
============
*/
void Cmd_AddCommand (char *cmd_name, xcommand_t function)
{
	cmd_function_t	*cmd;

	if (host_initialized)	// because hunk allocation would get stomped
		Sys_Error ("Cmd_AddCommand after host_initialized");

// fail if the command is a variable name
	if (Cvar_VariableString(cmd_name)[0])
	{
		Con_Printf ("Cmd_AddCommand: %s already defined as a var\n", cmd_name);
		return;
	}

// fail if the command already exists
	for (cmd = cmd_functions ; cmd ; cmd = cmd->next)
	{
		if ( !strcmp(cmd_name, cmd->name) )
		{
			Con_Printf ("Cmd_AddCommand: %s already defined\n", cmd_name);
			return;
		}
	}

	cmd = Hunk_AllocName (sizeof(cmd_function_t), "commands");
	cmd->name = cmd_name;
	cmd->function = function;
	cmd->next = cmd_functions;
	cmd_functions = cmd;
}

/*
============
Cmd_Exists
============
*/
qboolean Cmd_Exists (char *cmd_name)
{
	cmd_function_t	*cmd;

	for (cmd = cmd_functions ; cmd ; cmd = cmd->next)
	{
		if ( !strcmp(cmd_name, cmd->name) )
			return true;
	}

	return false;
}


/*
============
Cmd_CheckCommand
============
*/
qboolean Cmd_CheckCommand (char *partial)
{
	cmd_function_t	*cmd;
	cmdalias_t	*a;
	cvar_t		*var;

	if (!strlen(partial))
		return false;
	for (cmd = cmd_functions ; cmd ; cmd = cmd->next)
		if ( !strcmp(partial, cmd->name) )
			return true;
	for (var = cvar_vars ; var ; var = var->next)
		if ( !strcmp(partial, var->name) )
			return true;
	for (a = cmd_alias ; a ; a = a->next)
		if ( !strcmp(partial, a->name) )
			return true;

	return false;
}

/*
============
Cmd_ExecuteString

A complete command line has been parsed, so try to execute it
FIXME: lookupnoadd the token to speed search?
============
*/
void Cmd_ExecuteString (char *text, cmd_source_t src)
{
	cmd_function_t	*cmd;
	cmdalias_t		*a;

	cmd_source = src;
	Cmd_TokenizeString (text);

// execute the command line
	if (!Cmd_Argc())
		return;		// no tokens

// check functions
	for (cmd = cmd_functions ; cmd ; cmd = cmd->next)
	{
		if ( !Q_strcasecmp(cmd_argv[0], cmd->name) )
		{
#if defined(H2W)
			if (!cmd->function)
#  ifndef SERVERONLY
				Cmd_ForwardToServer ();
#  else
				Sys_Printf ("FIXME: command %s has NULL handler function\n", cmd->name);
#  endif
			else
#endif
				cmd->function ();

			return;
		}
	}

// check alias
	for (a = cmd_alias ; a ; a = a->next)
	{
		if ( !Q_strcasecmp(cmd_argv[0], a->name) )
		{
			Cbuf_InsertText (a->value);
			return;
		}
	}

// check cvars
	if (!Cvar_Command () && (cl_warncmd.value || developer.value))
		Con_Printf ("Unknown command \"%s\"\n", Cmd_Argv(0));
}

#if 0
/*
================
Cmd_CheckParm

Returns the position (1 to argc-1) in the command's argument list
where the given parameter apears, or 0 if not present
================
*/
int Cmd_CheckParm (char *parm)
{
	int	i;

	if (!parm)
		Sys_Error ("Cmd_CheckParm: NULL");

	for (i = 1; i < Cmd_Argc (); i++)
		if ( !Q_strcasecmp(parm, Cmd_Argv (i)) )
			return i;

	return 0;
}
#endif

#ifndef SERVERONLY
/*
===============
Cmd_List_f

Lists the commands to the console
===============
*/
int ListCommands (char *prefix, char **buf, int pos)
{
	cmd_function_t	*cmd;
	int	i = 0;
	int	preLen = (prefix == NULL) ? 0 : strlen(prefix);

	for (cmd = cmd_functions ; cmd ; cmd = cmd->next)
	{
		if (!preLen)	// completion procedures always send a prefix
		{
			Con_Printf (" %s\n", cmd->name);
			continue;
		}

		if ( !Q_strncasecmp(prefix, cmd->name, preLen) )
		{
			if (!buf)	// completion procedures always send a buf
			{
				Con_Printf (" %s\n", cmd->name);
				continue;
			}

			if (pos+i < MAX_MATCHES)
				buf[pos+i] = cmd->name;
			else
				break;

			i++;
		}
	}

	return i;
}

static void Cmd_List_f(void)
{
	if ( Cmd_Argc() > 1 )
		ListCommands (Cmd_Argv(1), NULL, 0);
	else
		ListCommands (NULL, NULL, 0);
}

/*
===============
Cmd_ListCvar_f

Lists the cvars to the console
===============
*/
int ListCvars (char *prefix, char **buf, int pos)
{
	cvar_t		*var;
	int i = 0;
	int preLen = (prefix == NULL) ? 0 : strlen(prefix);

	for (var = cvar_vars ; var ; var = var->next)
	{
		if (!preLen)	// completion procedures always send a prefix
		{
			Con_Printf (" %s\n", var->name);
			continue;
		}

		if ( !Q_strncasecmp(prefix, var->name, preLen) )
		{
			if (!buf)	// completion procedures always send a buf
			{
				Con_Printf (" %s\n", var->name);
				continue;
			}

			if (pos+i < MAX_MATCHES)
				buf[pos+i] = var->name;
			else
				break;

			i++;
		}
	}

	return i;
}

static void Cmd_ListCvar_f(void)
{
	if ( Cmd_Argc() > 1 )
		ListCvars (Cmd_Argv(1), NULL, 0);
	else
		ListCvars (NULL, NULL, 0);
}

/*
===============
Cmd_ListAlias_f

Lists the cvars to the console
===============
*/
int ListAlias (char *prefix, char **buf, int pos)
{
	cmdalias_t	*a;
	int	i = 0;
	int preLen = (prefix == NULL) ? 0 : strlen(prefix);

	for (a = cmd_alias ; a ; a = a->next)
	{
		if (!preLen)	// completion procedures always send a prefix
		{
			Con_Printf (" %s\n", a->name);
			continue;
		}

		if ( !Q_strncasecmp(prefix, a->name, preLen) )
		{
			if (!buf)	// completion procedures always send a buf
			{
				Con_Printf (" %s\n", a->name);
				continue;
			}

			if (pos+i < MAX_MATCHES)
				buf[pos+i] = a->name;
			else
				break;

			i++;
		}
	}

	return i;
}

static void Cmd_ListAlias_f(void)
{
	if ( Cmd_Argc() > 1 )
		ListAlias (Cmd_Argv(1), NULL, 0);
	else
		ListAlias (NULL, NULL, 0);
}

static void Cmd_WriteCommands_f (void)
{
	FILE	*FH;
	cmd_function_t	*cmd;
	cvar_t		*var;
	cmdalias_t	*a;

	FH = fopen(va("%s/commands.txt", com_userdir),"w");
	if (!FH)
	{
		return;
	}

	fprintf(FH,"\n\nConsole Commands:\n");
	for (cmd = cmd_functions ; cmd ; cmd = cmd->next)
		fprintf(FH, "   %s\n", cmd->name);

	fprintf(FH,"\n\nAlias Commands:\n");
	for (a = cmd_alias ; a ; a = a->next)
		fprintf(FH, "   %s :\n\t%s\n", a->name, a->value);

	fprintf(FH,"\n\nConsole Variables:\n");
	for (var = cvar_vars ; var ; var = var->next)
		fprintf(FH, "   %s\n", var->name);

	fclose(FH);
}
#endif

/*
============
Cmd_Init
============
*/
void Cmd_Init (void)
{
//
// register our commands
//
	Cmd_AddCommand ("stuffcmds",Cmd_StuffCmds_f);
	Cmd_AddCommand ("exec",Cmd_Exec_f);
	Cmd_AddCommand ("echo",Cmd_Echo_f);
	Cmd_AddCommand ("alias",Cmd_Alias_f);
	Cmd_AddCommand ("unalias",Cmd_Unalias_f);
	Cmd_AddCommand ("unaliasall",Cmd_Unaliasall_f);
	Cmd_AddCommand ("wait", Cmd_Wait_f);
#ifndef SERVERONLY
	Cmd_AddCommand ("commands", Cmd_WriteCommands_f);
	Cmd_AddCommand ("cmdlist", Cmd_List_f);
	Cmd_AddCommand ("cvarlist", Cmd_ListCvar_f);
	Cmd_AddCommand ("aliaslist", Cmd_ListAlias_f);
#endif
}

