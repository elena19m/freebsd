#
# $FreeBSD$
#

.PATH: ${BHYVE_SRCTOP}/mmio/
SRCS+=	\
	mmio_virtio_block.c	\
	mmio_virtio_console.c	\
	mmio_virtio_net.c	\
	mmio_virtio_rnd.c	\
	mmio_emul.c		\
	mmio_irq.c		\
	net_utils.c		\
	mmio_virtio.c


CFLAGS+=	-I${BHYVE_SRCTOP}/mmio
