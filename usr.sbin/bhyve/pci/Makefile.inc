#
# $FreeBSD$
#

.PATH: ${BHYVE_SRCTOP}/pci/
SRCS+=	\
	devemu_irq.c		\
	devemu_virtio.c		\
	devemu.c		\
	pci_ahci.c		\
	pci_e82545.c		\
	pci_fbuf.c		\
	pci_hostbridge.c	\
	pci_lpc.c		\
	pci_passthru.c		\
	pci_uart.c		\
	pci_xhci.c


CFLAGS+=	-I${BHYVE_SRCTOP}/pci
