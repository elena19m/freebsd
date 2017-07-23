#
# $FreeBSD$
#
.PATH: ${BHYVE_SRCTOP}/arm64/
SRCS=	bhyverun.c block_if.c mem.c mevent.c consport.c

.PATH:	${.CURDIR}/../../sys/arm64/vmm

MK_MAN=no
SRCS+=	vmm_instruction_emul.c

LIBADD=
