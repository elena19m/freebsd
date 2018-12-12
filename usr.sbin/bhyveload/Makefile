# $FreeBSD$

PROG=	bhyveload
PACKAGE=	bhyve

BHYVELOAD_SYSDIR?=${SRCTOP}
BHYVELOAD_SRCTOP?=${.CURDIR}

LIBADD=	vmmapi

WARNS?=	3

CFLAGS+=-I${SRCTOP}/stand/userboot

.include "${BHYVELOAD_SRCTOP}/${MACHINE}/Makefile.inc"

.include <bsd.prog.mk>
