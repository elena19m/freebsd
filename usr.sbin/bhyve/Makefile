#
# $FreeBSD$
#

.include <src.opts.mk>
CFLAGS+=-I${.CURDIR}/../../contrib/lib9p
CFLAGS+=-I${SRCTOP}/sys
.PATH:  ${SRCTOP}/sys/cam/ctl

PROG=	bhyve
PACKAGE=	bhyve

MAN=	bhyve.8

BHYVE_SYSDIR?=${SRCTOP}
BHYVE_SRCTOP?=${.CURDIR}

#	audio.c			
#	ctl_util.c		
#	ctl_scsi_all.c		
#	fwctl.c			
#	hda_codec.c		
#	pci_hda.c		
#	pci_virtio_scsi.c	
#	pci_uart.c		
#	post.c			


SRCS=	\
	net_backends.c		\
	net_utils.c		\
	pci_ahci.c		\
	pci_e82545.c		\
	pci_emul.c		\
	pci_hda.c		\
	pci_fbuf.c		\
	pci_hostbridge.c	\
	pci_irq.c		\
	pci_lpc.c		\
	pci_nvme.c		\
	pci_passthru.c		\
	pci_virtio_9p.c		\
	pci_virtio_block.c	\
	pci_virtio_console.c	\
	pci_virtio_net.c	\
	pci_virtio_rnd.c	\
	pci_virtio_scsi.c	\
	pci_uart.c		\
	pci_xhci.c		\
	pctestdev.c		\
	pm.c			\
	post.c			\
	ps2kbd.c		\
	ps2mouse.c		\
	rfb.c			\
	rtc.c			\
	smbiostbl.c		\
	sockstream.c		\
	task_switch.c		\
	uart_emul.c		\
	usb_emul.c		\
	usb_mouse.c		\
	virtio.c		\
	vga.c			\
	vmgenc.c		\
	xmsr.c			\
	spinup_ap.c		\
	iov.c			\
	block_if.c		\
	mevent.c		\
	sockstream.c

.if ${MACHINE_CPUARCH} == "aarch64"
# Missing vm_get_ioctls
CFLAGS+=	-DWITHOUT_CAPSICUM

#SRCS+=	\

.endif

.if ${MACHINE_CPUARCH} == "amd64"
SRCS+=	\
	pci_virtio_block.c	\
	pci_virtio_console.c	\
	pci_virtio_net.c	\
	pci_virtio_rnd.c	\
	pci_emul.c		\
	pci_irq.c		\
	pci_virtio.c		\
	net_utils.c		\
	acpi.c			\
	atkbdc.c		\
	bhyvegc.c		\
	bhyverun.c		\
	bootrom.c		\
	console.c		\
	consport.c		\
	dbgport.c		\
	gdb.c			\
	inout.c			\
	ioapic.c		\
	mem.c			\
	mptbl.c			\
	pci_ahci.c		\
	pci_hostbridge.c	\
	pci_e82545.c		\
	pci_fbuf.c		\
	pci_lpc.c		\
	pci_nvme.c		\
	pci_passthru.c		\
	pci_xhci.c		\
	pm.c			\
	ps2kbd.c		\
	ps2mouse.c		\
	uart_emul.c		\
	usb_emul.c		\
	usb_mouse.c		\
	rfb.c			\
	rtc.c			\
	task_switch.c		\
	xmsr.c			\
	smbiostbl.c		\
	spinup_ap.c		\
	vga.c
.endif

LIBADD=	vmmapi md pthread

.if ${MK_INET_SUPPORT} != "no"
CFLAGS+=-DINET
.endif
.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+=-DINET6
.endif
.if ${MK_NETGRAPH_SUPPORT} != "no"
CFLAGS+=-DNETGRAPH
LIBADD+=    netgraph
.endif
.if ${MK_OPENSSL} == "no"
CFLAGS+=-DNO_OPENSSL
.endif

.if exists(${BHYVE_SRCTOP}/${MACHINE})
BHYVE_ARCH=	${MACHINE}
.elif exists(${BHYVE_SRCTOP}/${MACHINE_ARCH})
BHYVE_ARCH=	${MACHINE_ARCH}
.else
BHYVE_ARCH=	${MACHINE_CPUARCH}
.endif

.if ${MK_BHYVE_SNAPSHOT} != "no"
CFLAGS+= -I${SRCTOP}/contrib/libucl/include

# Temporary disable capsicum, until we integrate checkpoint code with it.
CFLAGS+= -DWITHOUT_CAPSICUM

CFLAGS+= -DBHYVE_SNAPSHOT
.endif
.PATH:	${BHYVE_SYSDIR}/sys/${BHYVE_ARCH}/vmm
SRCS+=	vmm_instruction_emul.c

.include "${BHYVE_SRCTOP}/${BHYVE_ARCH}/Makefile.inc"
.include "${BHYVE_SRCTOP}/${BHYVE_BUS}/Makefile.inc"

CFLAGS+=	-I${BHYVE_SRCTOP}
CFLAGS+=	-I${BHYVE_SRCTOP}/${BHYVE_ARCH}
CFLAGS+= 	-I${BHYVE_SYSDIR}/sys/dev/virtio
CFLAGS+= 	-I${BHYVE_SYSDIR}/sys/dev/virtio/console

.ifdef GDB_LOG
CFLAGS+=-DGDB_LOG
.endif

WARNS?=	2

.include <bsd.prog.mk>
