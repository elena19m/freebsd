#
# $FreeBSD$
#

.PATH: ${BHYVE_SRCTOP}/mmio/
SRCS+=	\
	mmio_emul.c		\
	mmio_irq.c		\
	mmio_virtio.c


CFLAGS+=	-I${BHYVE_SRCTOP}/mmio
