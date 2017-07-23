# $FreeBSD$
.PATH: ${BHYVELOAD_SRCTOP}/arm64/

SRCS=	bhyveload.c

.PATH:	${.CURDIR}/../../sys/arm64/vmm

MK_MAN=no
