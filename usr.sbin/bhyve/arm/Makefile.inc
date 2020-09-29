#
# $FreeBSD$
#
.PATH: ${BHYVE_SRCTOP}/arm/
SRCS+=				\
	arm/bhyverun.c		\
	arm/consport.c		\
	arm/mem.c		\
	arm/reset.c

.PATH:	${BHYVE_SYSDIR}/sys/${BHYVE_ARCH}/vmm
MK_MAN=no

BHYVE_BUS=	mmio

CFLAGS+= 	-I${BHYVE_SYSDIR}/sys/dev/virtio/mmio
