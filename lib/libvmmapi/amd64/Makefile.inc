# $FreeBSD$
.PATH: ${LIB_SRCTOP}/amd64/

SRCS=	vmmapi.c vmmapi_freebsd.c
INCS=	vmmapi.h

LIBADD=	util
