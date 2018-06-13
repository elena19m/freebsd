#
# $FreeBSD$
#
.PATH: ${BHYVE_SRCTOP}/arm/
SRCS+=				\
	bhyverun.c		\
	consport.c		\
	mem.c			\
	reset.c

.PATH:	${BHYVE_SYSDIR}/sys/${BHYVE_ARCH}/vmm
MK_MAN=no

LIBADD=

BHYVE_BUS=	mmio

CFLAGS+= 	-I${BHYVE_SYSDIR}/sys/dev/virtio/mmio
