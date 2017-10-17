# $FreeBSD$
.PATH: ${BHYVELOAD_SRCTOP}/arm64/

SRCS=	bhyveload.c \
	boot.c

.PATH:	${.CURDIR}/../../sys/arm64/vmm

CFLAGS += -I${.CURDIR}/../../sys/boot/common

MK_MAN=no
