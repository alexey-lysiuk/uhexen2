/* Macros for taking apart, interpreting and processing file names.
 *
 * These are here because some non-Posix (a.k.a. DOSish) systems have
 * drive letter brain-damage at the beginning of an absolute file name,
 * use forward- and back-slash in path names interchangeably, and
 * some of them have case-insensitive file names.
 *
 * Copyright 2000, 2001, 2007 Free Software Foundation, Inc.
 *
 * This is based on filenames.h from BFD, the Binary File Descriptor
 * library, changed further for our needs.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#ifndef FILENAMES_H
#define FILENAMES_H

#include <string.h>
#include "compiler.h" /* for inline, etc */

/* ---------------------- Windows, DOS, OS2: ---------------------- */
#if defined(__MSDOS__) || defined(MSDOS) || defined(__DOS__) || \
	defined(_WIN32) || defined(__OS2__) || defined(__CYGWIN__)

#define HAVE_DOS_BASED_FILE_SYSTEM 1
#define HAVE_CASE_INSENSITIVE_FILE_SYSTEM 1

#define HAS_DRIVE_SPEC(f)	((f)[0] && ((f)[1] == ':'))
#define STRIP_DRIVE_SPEC(f)	((f) + 2)
#define IS_DIR_SEPARATOR(c)	((c) == '/' || (c) == '\\')
/* both '/' and '\\' work as dir separator.  djgpp likes changing
 * '\\' into '/', so I define DIR_SEPARATOR_CHAR as '/' for djgpp,
 * '\\' otherwise.  */
#ifdef __DJGPP__
#define DIR_SEPARATOR_CHAR	'/'
#else
#define DIR_SEPARATOR_CHAR	'\\'
#endif
/* Note that IS_ABSOLUTE_PATH accepts d:foo as well, although it is
   only semi-absolute.  This is because the users of IS_ABSOLUTE_PATH
   want to know whether to prepend the current working directory to
   a file name, which should not be done with a name like d:foo.  */
#define IS_ABSOLUTE_PATH(f)	(IS_DIR_SEPARATOR((f)[0]) || HAS_DRIVE_SPEC((f)))

#ifdef __cplusplus
static inline char *FIND_FIRST_DIRSEP(char *_the_path) {
/* FIXME: What about C:FOO ? */
    char *p1 = strchr(_the_path, '/');
    char *p2 = strchr(_the_path, '\\');
    if (p1 == NULL) return p2;
    if (p2 == NULL) return p1;
    return (p1 < p2)? p1 : p2;
}
static inline char *FIND_LAST_DIRSEP (char *_the_path) {
/* FIXME: What about C:FOO ? */
    char *p1 = strrchr(_the_path, '/');
    char *p2 = strrchr(_the_path, '\\');
    if (p1 == NULL) return p2;
    if (p2 == NULL) return p1;
    return (p1 > p2)? p1 : p2;
}
static inline const char *FIND_FIRST_DIRSEP(const char *_the_path) {
/* FIXME: What about C:FOO ? */
    const char *p1 = strchr(_the_path, '/');
    const char *p2 = strchr(_the_path, '\\');
    if (p1 == NULL) return p2;
    if (p2 == NULL) return p1;
    return (p1 < p2)? p1 : p2;
}
static inline const char *FIND_LAST_DIRSEP (const char *_the_path) {
/* FIXME: What about C:FOO ? */
    const char *p1 = strrchr(_the_path, '/');
    const char *p2 = strrchr(_the_path, '\\');
    if (p1 == NULL) return p2;
    if (p2 == NULL) return p1;
    return (p1 > p2)? p1 : p2;
}
#else
static inline char *FIND_FIRST_DIRSEP(const char *_the_path) {
/* FIXME: What about C:FOO ? */
    char *p1 = strchr(_the_path, '/');
    char *p2 = strchr(_the_path, '\\');
    if (p1 == NULL) return p2;
    if (p2 == NULL) return p1;
    return (p1 < p2)? p1 : p2;
}
static inline char *FIND_LAST_DIRSEP (const char *_the_path) {
/* FIXME: What about C:FOO ? */
    char *p1 = strrchr(_the_path, '/');
    char *p2 = strrchr(_the_path, '\\');
    if (p1 == NULL) return p2;
    if (p2 == NULL) return p1;
    return (p1 > p2)? p1 : p2;
}
#endif /* C++ */

/* ----------------- AmigaOS, MorphOS, AROS, etc: ----------------- */
#elif defined(__amigados__) || defined(__amigaos4__) || defined(__AMIGA) || \
	defined(__amigaos__) || defined(__MORPHOS__) || defined(__AROS__)

#define HAS_DRIVE_SPEC(f)	(0) /* */
#define STRIP_DRIVE_SPEC(f)	(f) /* */
#define IS_DIR_SEPARATOR(c)	((c) == '/' || (c) == ':')
#define DIR_SEPARATOR_CHAR	'/'
#define IS_ABSOLUTE_PATH(f)	(IS_DIR_SEPARATOR((f)[0]) || (strchr((f), ':')))
#define HAVE_CASE_INSENSITIVE_FILE_SYSTEM 1

#ifdef __cplusplus
static inline char *FIND_FIRST_DIRSEP(char *_the_path) {
    char *p = strchr(_the_path, ':');
    if (p != NULL) return p;
    return strchr(_the_path, '/');
}
static inline char *FIND_LAST_DIRSEP (char *_the_path) {
    char *p = strrchr(_the_path, '/');
    if (p != NULL) return p;
    return strchr(_the_path, ':');
}
static inline const char *FIND_FIRST_DIRSEP(const char *_the_path) {
    const char *p = strchr(_the_path, ':');
    if (p != NULL) return p;
    return strchr(_the_path, '/');
}
static inline const char *FIND_LAST_DIRSEP (const char *_the_path) {
    const char *p = strrchr(_the_path, '/');
    if (p != NULL) return p;
    return strchr(_the_path, ':');
}
#else
static inline char *FIND_FIRST_DIRSEP(const char *_the_path) {
    char *p = strchr(_the_path, ':');
    if (p != NULL) return p;
    return strchr(_the_path, '/');
}
static inline char *FIND_LAST_DIRSEP (const char *_the_path) {
    char *p = strrchr(_the_path, '/');
    if (p != NULL) return p;
    return strchr(_the_path, ':');
}
#endif /* C++ */

/* ------------------------ assumed UNIX : ------------------------ */
#else /* */

#define IS_DIR_SEPARATOR(c)	((c) == '/')
#define DIR_SEPARATOR_CHAR	'/'
#define IS_ABSOLUTE_PATH(f)	(IS_DIR_SEPARATOR((f)[0]))
#define HAS_DRIVE_SPEC(f)	(0)
#define STRIP_DRIVE_SPEC(f)	(f)

#ifdef __cplusplus
static inline char *FIND_FIRST_DIRSEP(char *_the_path) {
    return strchr(_the_path, '/');
}
static inline char *FIND_LAST_DIRSEP (char *_the_path) {
    return strrchr(_the_path, '/');
}
static inline const char *FIND_FIRST_DIRSEP(const char *_the_path) {
    return strchr(_the_path, '/');
}
static inline const char *FIND_LAST_DIRSEP (const char *_the_path) {
    return strrchr(_the_path, '/');
}
#else
static inline char *FIND_FIRST_DIRSEP(const char *_the_path) {
    return strchr(_the_path, '/');
}
static inline char *FIND_LAST_DIRSEP (const char *_the_path) {
    return strrchr(_the_path, '/');
}
#endif /* C++ */

#endif

#endif /* FILENAMES_H */

