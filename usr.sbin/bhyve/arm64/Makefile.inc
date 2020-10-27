#
# $FreeBSD$
#
.PATH: ${BHYVE_SRCTOP}/arm64/
SRCS+=				\
	arm64/bhyverun.c 	\
	arm64/mem.c		\
	arm64/consport.c	\
	arm64/reset.c

.PATH:	${BHYVE_SYSDIR}/sys/${BHYVE_ARCH}/vmm

MK_MAN=no

BHYVE_BUS= mmio

CFLAGS+= 	-I${BHYVE_SYSDIR}/sys/dev/virtio/mmio
