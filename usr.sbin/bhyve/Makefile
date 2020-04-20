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

SRCS=	\
	audio.c			\
	net_backends.c		\
	net_utils.c		\
	block_if.c		\
	pci_virtio_block.c	\
	pci_virtio_console.c	\
	pci_virtio_net.c	\
	pci_virtio_rnd.c	\
	mevent.c		\
	sockstream.c

.if ${MK_BHYVE_SNAPSHOT} != "no"
SRCS+=	snapshot.c
.endif

CFLAGS.kernemu_dev.c+=	-I${SRCTOP}/sys/amd64

.PATH:	${BHYVE_SYSDIR}/sys/${BHYVE_ARCH}/vmm
SRCS+=	vmm_instruction_emul.c

LIBADD=	vmmapi md pthread z util sbuf cam 9p casper cap_pwd cap_grp
.if ${MK_BHYVE_SNAPSHOT} != "no"
LIBADD+= ucl xo
.endif

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
