#
# $FreeBSD$
#

.PATH: ${BHYVE_SRCTOP}/pci/
SRCS+=	\
	devemu_irq.c		\
	devemu_virtio.c		\
	devemu.c


CFLAGS+=	-I${BHYVE_SRCTOP}/pci
