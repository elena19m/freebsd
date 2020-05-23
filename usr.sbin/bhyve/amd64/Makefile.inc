#
# $FreeBSD$
#
.PATH: ${BHYVE_SRCTOP}/amd64/

MAN=	bhyve.8

SRCS+=	\
	atkbdc.c		\
	acpi.c			\
	bhyvegc.c		\
	bhyverun.c		\
	iblock_if.c		\
	bootrom.c		\
	console.c		\
	consport.c		\
	dbgport.c		\
	fwctl.c			\
	gdb.c			\
	hda_codec.c		\
	inout.c			\
	ioapic.c		\
	mem.c			\
	mptbl.c			\
	net_backends.c		\
	net_utils.c		\
	pci_hda.c		\
	pci_nvme.c		\
	pm.c			\
	post.c			\
	ps2kbd.c		\
	ps2mouse.c		\
	rfb.c			\
	rtc.c			\
	smbiostbl.c		\
	task_switch.c		\
	uart_emul.c		\
	usb_emul.c		\
	usb_mouse.c		\
	vga.c			\
	virtio.c		\
	xmsr.c			\
	spinup_ap.c

LIBADD+=	z util sbuf cam

.if ${MK_OPENSSL} == "no"
CFLAGS+=-DNO_OPENSSL
.else
LIBADD+=	crypto
.endif

CFLAGS+=	-I${BHYVE_SYSDIR}/sys/dev/e1000
CFLAGS+=	-I${BHYVE_SYSDIR}/sys/dev/mii
CFLAGS+=	-I${BHYVE_SYSDIR}/sys/dev/usb/controller

BHYVE_BUS=	pci
