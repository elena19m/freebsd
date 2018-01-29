#
# $FreeBSD$
#
.PATH: ${BHYVE_SRCTOP}/arm/
SRCS=				\
	bhyverun.c		\
	block_if.c		\
	consport.c		\
	mem.c			\
	mevent.c		\
	mmio_emul.c		\
	mmio_irq.c		\
	mmio_virtio_console.c	\
	mmio_virtio_net.c	\
	mmio_virtio_rnd.c	\
	sockstream.c		\
	reset.c			\
	virtio.c

.PATH:	${.CURDIR}/../../sys/arm/vmm

MK_MAN=no
SRCS+=	vmm_instruction_emul.c

LIBADD=

CFLAGS+= -I${BHYVE_SYSDIR}/sys/dev/virtio
CFLAGS+= -I${BHYVE_SYSDIR}/sys/dev/virtio/mmio
