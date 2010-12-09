#!/bin/sh

# $Header: /home/ozzie/Download/0000/uhexen2/hexen2/build_cross_dos.sh,v 1.5 2009-04-21 08:40:47 sezero Exp $
# used for building h2dos.exe with djgpp cross toolchain

TARGET=i586-pc-msdosdjgpp
PREFIX=/usr/local/cross-djgpp

PATH="$PREFIX/bin:$PATH"
export PATH

DOSBUILD=1

CC="$TARGET-gcc"
AS="$TARGET-as"
AR="$TARGET-ar"

export CC AS AR DOSBUILD

HOST_OS=`uname|sed -e s/_.*//|tr '[:upper:]' '[:lower:]'`

case "$HOST_OS" in
freebsd|openbsd|netbsd)
	MAKE_CMD=gmake
	;;
linux)
	MAKE_CMD=make
	;;
*)
	MAKE_CMD=make
	;;
esac

$MAKE_CMD $2 $3 $4 $5 $6 h2  || exit 1
