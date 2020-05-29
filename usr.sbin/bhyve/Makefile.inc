#
# $FreeBSD$
#

.PATH: ${BHYVE_SRCTOP}/pci/
SRCS+=	\
	pci_ahci.c		\
	pci_e82545.c		\
	pci_emul.c		\
	pci_fbuf.c		\
	pci_hostbridge.c	\
	pci_irq.c		\
	pci_lpc.c		\
	pci_passthru.c		\
	pci_uart.c		\
	pci_virtio_block.c	\
	pci_virtio_net.c	\
	pci_virtio_scsi.c	\
	pci_virtio_console.c	\
	pci_virtio_rnd.c	\
	pci_xhci.c

CFLAGS+=	-I${BHYVE_SRCTOP}/pci
