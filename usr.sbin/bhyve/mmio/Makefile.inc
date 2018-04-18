#
# $FreeBSD$
#

.PATH: ${BHYVE_SRCTOP}/mmio/
SRCS+=	\
	devemu_irq.c		\
	devemu_virtio.c		\
	devemu.c

CFLAGS+=	-I${BHYVE_SRCTOP}/mmio
