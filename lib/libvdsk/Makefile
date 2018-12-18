# $FreeBSD: user/marcel/libvdsk/libvdsk/Makefile 275095 2014-11-26 00:58:36Z marcel $

LIB=	vdsk
SHLIB_MAJOR= 0

SRCS=	vdsk.c
INCS=	vdsk.h /usr/src/usr.sbin/bhyve/block_if.h

# List of formats to support
SRCS+=	\
	raw.c \
	qcow.c

DEBUG_FLAGS=-O0 -gdwarf-2

WARNS?= 2

LIBADD=	pthread

.include <bsd.lib.mk>
