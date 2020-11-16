#
# $FreeBSD$
#

.PATH: ${BHYVE_SRCTOP}/mmio/
SRCS+=	\
	mmio/mmio_virtio_block.c	\
	mmio/mmio_virtio_console.c	\
	mmio/mmio_virtio_net.c		\
	mmio/mmio_virtio_rnd.c		\
	mmio/mmio_emul.c		\
	mmio/mmio_irq.c			\
	mmio/net_utils.c		\
	mmio/mmio_virtio.c


CFLAGS+=	-I${BHYVE_SRCTOP}/mmio
