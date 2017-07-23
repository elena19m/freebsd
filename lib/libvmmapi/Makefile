# $FreeBSD$

PACKAGE=	lib${LIB}
SHLIBDIR?=	/lib
LIB_SRCTOP?=	${.CURDIR}

LIB=		vmmapi
WARNS?=	2

.if exists(${LIB_SRCTOP}/${MACHINE})
LIB_ARCH=	${MACHINE}
.elif exists(${LIB_SRCTOP}/${MACHINE_ARCH})
LIB_ARCH=	${MACHINE_ARCH}
.else
LIB_ARCH=	${MACHINE_CPUARCH}
.endif

CFLAGS+= 	-I${LIB_SRCTOP}/${LIB_ARCH}
.include 	"${LIB_SRCTOP}/${LIB_ARCH}/Makefile.inc"

.include <bsd.lib.mk>
