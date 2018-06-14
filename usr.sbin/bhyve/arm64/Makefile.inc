#
# $FreeBSD$
#
.PATH: ${BHYVE_SRCTOP}/arm64/
SRCS+=			\
	bhyverun.c 	\
	mem.c 		\
	consport.c

.PATH:	${.CURDIR}/../../sys/arm64/vmm

MK_MAN=no

BHYVE_BUS= mmio

CFLAGS+= 	-I${BHYVE_SYSDIR}/sys/dev/virtio/mmio
