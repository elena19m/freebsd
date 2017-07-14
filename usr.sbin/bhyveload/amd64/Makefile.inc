# $FreeBSD$
.PATH: ${BHYVELOAD_SRCTOP}/amd64/

SRCS=	bhyveload.c
MAN=	bhyveload.8

CFLAGS+=-I${SRCTOP}/sys/boot/userboot
