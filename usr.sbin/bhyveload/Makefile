# $FreeBSD$

PROG=	bhyveload
PACKAGE=	bhyve

BHYVELOAD_SYSDIR?=${SRCTOP}
BHYVELOAD_SRCTOP?=${.CURDIR}

.if exists(${BHYVELOAD_SRCTOP}/${MACHINE_ARCH})
BHYVELOAD_ARCH=	${MACHINE_ARCH}
.else
BHYVELOAD_ARCH=	${MACHINE_CPUARCH}
.endif

LIBADD=	vmmapi

WARNS?=	3

CFLAGS+=-I${SRCTOP}/stand/userboot

.include "${BHYVELOAD_SRCTOP}/${BHYVELOAD_ARCH}/Makefile.inc"

.include <bsd.prog.mk>
