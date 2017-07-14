#
# $FreeBSD$
#
.PATH: ${BHYVE_SRCTOP}/arm/
SRCS=	bhyverun.c block_if.c mem.c mevent.c consport.c

.PATH:	${.CURDIR}/../../sys/arm/vmm

MK_MAN=no
SRCS+=	vmm_instruction_emul.c

LIBADD=
